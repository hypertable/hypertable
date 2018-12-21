/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <Common/Compat.h>

#ifdef HT_WITH_THRIFT
#include <ThriftBroker/Config.h>
#include <ThriftBroker/Client.h>
#endif

#include <Hyperspace/Session.h>

#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/Master/Client.h>
#include <Hypertable/Lib/RangeServer/Client.h>
#include <Hypertable/Lib/TableIdentifier.h>

#include <FsBroker/Lib/Client.h>

#include <AsyncComm/ApplicationQueue.h>
#include <AsyncComm/Comm.h>
#include <AsyncComm/ConnectionManager.h>
#include <AsyncComm/ReactorFactory.h>

#include <Common/Error.h>
#include <Common/InetAddr.h>
#include <Common/Logger.h>
#include <Common/Init.h>
#include <Common/Status.h>
#include <Common/Timer.h>
#include <Common/Usage.h>

#include <boost/algorithm/string.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

extern "C" {
#include <netdb.h>
#include <sys/types.h>
#include <signal.h>
}

using namespace Hypertable;
using namespace Hypertable::Lib;
using namespace Config;
using namespace std;

namespace {

  const char *usage =
    "Usage: serverup [options] <server-name>\n\n"
    "Description:\n"
    "  This program checks to see if the server, specified by <server-name>\n"
    "  is up. return 0 if true, 1 otherwise. <server-name> may be one of the\n"
    "  following values: fsbroker, hyperspace, master, global-master, \n"
    "  rangeserver, thriftbroker\n\n"
    "  master: checks for a master running on localhost\n"
    "  global-master: checks for a master running in the cluster (address is\n"
    "    fetched from hyperspace)\n"
    "Options";

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
          ("wait", i32(2000), "Check wait time in ms")
          ("host", str(), "Specifies the hostname of the server(s)")
          ("display-address", boo(false),
           "Displays hostname and port of the server(s), then exits")
          ("thrift-transport", str("framed"), "thrift-transport")
          ;
      cmdline_hidden_desc().add_options()
       ("server-name", str(), "")
       ("server-name", -1);
    }
    static void init() {
      // we want to override the default behavior that verbose
      // turns on debugging by clearing the defaulted flag
      if (defaulted("logging-level"))
        properties->set("logging-level", gEnumExt(Logger::Priority::FATAL));
    }
  };

#ifdef HT_WITH_THRIFT
  typedef Meta::list<AppPolicy, FsClientPolicy, HyperspaceClientPolicy,
          MasterClientPolicy, RangeServerClientPolicy, ThriftClientPolicy,
          DefaultCommPolicy> Policies;
#else
  typedef Meta::list<AppPolicy, FsClientPolicy, HyperspaceClientPolicy,
          MasterClientPolicy, RangeServerClientPolicy, DefaultCommPolicy>
          Policies;
#endif

  void
  wait_for_connection(const char *server, ConnectionManagerPtr &conn_mgr,
                      InetAddr &addr, int timeout_ms, int wait_ms) {
    HT_DEBUG_OUT <<"Checking "<< server <<" at "<< addr << HT_END;

    conn_mgr->add(addr, timeout_ms, server);

    if (!conn_mgr->wait_for_connection(addr, wait_ms))
      HT_THROWF(Error::REQUEST_TIMEOUT, "connecting to %s", server);
  }

  void check_fsbroker(ConnectionManagerPtr &conn_mgr, uint32_t wait_ms) {
    HT_DEBUG_OUT << "Checking fsbroker at " << get_str("fs-host")
        << ':' << get_i16("fs-port") << HT_END;

    if (properties->has("host")) {
      properties->set("FsBroker.Host", properties->get_str("host"));
      properties->set("fs-host", properties->get_str("host"));
    }

    if (get_bool("display-address")) {
      std::cout << get_str("fs-host") << ":" << get_i16("fs-port")
          << std::endl;
      quick_exit(EXIT_SUCCESS);
    }

    FsBroker::Lib::ClientPtr fs = std::make_shared<FsBroker::Lib::Client>(conn_mgr, properties);

    if (!fs->wait_for_connection(wait_ms))
      HT_THROW(Error::REQUEST_TIMEOUT, "connecting to fsbroker");

    try {
      Status::Code code;
      string output;
      Status status;
      fs->status(status);
      status.get(&code, output);
      if (code != Status::Code::OK && code != Status::Code::WARNING)
        HT_THROW(Error::FAILED_EXPECTATION, output);
    }
    catch (Exception &e) {
      HT_ERRORF("Status check: %s - %s", Error::get_text(e.code()), e.what());
      quick_exit(EXIT_FAILURE);
    }
  }

  Hyperspace::SessionPtr hyperspace;

  void check_hyperspace(ConnectionManagerPtr &conn_mgr, uint32_t max_wait_ms) {
    HT_DEBUG_OUT << "Checking hyperspace"<< HT_END;
    Timer timer(max_wait_ms, true);
    int error;

    String host = "localhost";
    if (properties->has("host")) {
      host = properties->get_str("host");
      std::vector<String> vec;
      vec.push_back(host);
      properties->set("Hyperspace.Replica.Host", (gStrings)vec);
    }

    if (get_bool("display-address")) {
      std::cout << host << ":" <<
          properties->get_i16("Hyperspace.Replica.Port") << std::endl;
      quick_exit(EXIT_SUCCESS);
    }

    hyperspace = make_shared<Hyperspace::Session>(conn_mgr->get_comm(), properties);

    if (!hyperspace->wait_for_connection(max_wait_ms))
      HT_THROW(Error::REQUEST_TIMEOUT, "connecting to hyperspace");

    Status status;
    error = hyperspace->status(status, &timer);
    if (error == Error::OK) {
      if (status.get() != Status::Code::OK)
        HT_THROW(Error::FAILED_EXPECTATION, "getting hyperspace status");
    }
    else if (error != Error::HYPERSPACE_NOT_MASTER_LOCATION) {
      HT_THROW(error, "getting hyperspace status");
    }
  }

  void check_global_master(ConnectionManagerPtr &conn_mgr, uint32_t wait_ms) {
    HT_DEBUG_OUT << "Checking master via hyperspace" << HT_END;
    Timer timer(wait_ms, true);

    if (get_bool("display-address")) {
      std::cout << get_str("Hypertable.Master.Host") << ":" <<
          get_i16("Hypertable.Master.Port") << std::endl;
      quick_exit(EXIT_SUCCESS);
    }

    if (!hyperspace) {
      hyperspace = make_shared<Hyperspace::Session>(conn_mgr->get_comm(), properties);
      if (!hyperspace->wait_for_connection(wait_ms))
        HT_THROW(Error::REQUEST_TIMEOUT, "connecting to hyperspace");
    }

    ApplicationQueueInterfacePtr app_queue = make_shared<ApplicationQueue>(1);

    String toplevel_dir = properties->get_str("Hypertable.Directory");
    boost::trim_if(toplevel_dir, boost::is_any_of("/"));
    toplevel_dir = String("/") + toplevel_dir;

    Lib::Master::Client *master =
      new Lib::Master::Client(conn_mgr, hyperspace,
                              toplevel_dir, wait_ms, app_queue,
                              DispatchHandlerPtr(), ConnectionInitializerPtr());

    if (!master->wait_for_connection(wait_ms))
      HT_THROW(Error::REQUEST_TIMEOUT, "connecting to master");

    Status status;
    Status::Code code;
    string text;
    master->status(status, &timer);
    status.get(&code, text);
    if (code != Status::Code::OK)
      HT_THROW(Error::FAILED_EXPECTATION, text);
  }

  void check_master(ConnectionManagerPtr &conn_mgr, uint32_t wait_ms) {
    Timer timer(wait_ms, true);
    uint16_t port = properties->get_i16("Hypertable.Master.Port");

    const char *host;
    if (properties->has("host"))
      host = properties->get_str("host").c_str();
    else
      host = "localhost";

    if (get_bool("display-address")) {
      std::cout << host << ":" << port << std::endl;
      quick_exit(EXIT_SUCCESS);
    }

    HT_DEBUG_OUT << "Checking master on " << host << ":" << port << HT_END;
    InetAddr addr(host, port);

    Lib::Master::Client *master = new Lib::Master::Client(conn_mgr, addr, wait_ms);

    if (!master->wait_for_connection(wait_ms)) {
      if (strcmp(host, "localhost") && strcmp(host, "127.0.0.1"))
        HT_THROW(Error::REQUEST_TIMEOUT, "connecting to master");

      String pidstr;
      String pid_file = System::install_dir + "/run/Master.pid";
      if (!FileUtils::read(pid_file, pidstr))
        HT_THROW(Error::FILE_NOT_FOUND, pid_file);
      pid_t pid = (pid_t)strtoul(pidstr.c_str(), 0, 0);
      if (pid <= 0)
        HT_THROW(Error::REQUEST_TIMEOUT, "connecting to master");
      // (kill(pid, 0) does not send any signal but checks if the process exists
      if (::kill(pid, 0) < 0)
        HT_THROW(Error::REQUEST_TIMEOUT, "connecting to master");
    }
    else {
      Status status;
      Status::Code code;
      string text;
      master->status(status, &timer);
      status.get(&code, text);
      if (code != Status::Code::OK)
        HT_THROW(Error::FAILED_EXPECTATION, text);
    }
  }

  void check_rangeserver(ConnectionManagerPtr &conn_mgr, uint32_t wait_ms) {
    HT_DEBUG_OUT <<"Checking rangeserver at "<< get_str("rs-host")
                 <<':'<< get_i16("rs-port") << HT_END;

    Status::Code ready_status =
      Status::string_to_code(properties->get_str("Hypertable.RangeServer.ReadyStatus"));

    if (properties->has("host"))
      properties->set("rs-host", properties->get_str("host"));

    if (get_bool("display-address")) {
      std::cout << get_str("rs-host") << ":" << get_i16("rs-port") << std::endl;
      quick_exit(EXIT_SUCCESS);
    }

    InetAddr addr(get_str("rs-host"), get_i16("rs-port"));

    wait_for_connection("range server", conn_mgr, addr, wait_ms, wait_ms);

    RangeServer::ClientPtr range_server
      = make_shared<RangeServer::Client>(conn_mgr->get_comm(), wait_ms);
    Timer timer(wait_ms, true);
    Status status;
    range_server->status(addr, status, timer);
    Status::Code code;
    string text;
    status.get(&code, text);
    if (code > ready_status)
      HT_THROW(Error::FAILED_EXPECTATION, text);
  }

  void check_thriftbroker(ConnectionManagerPtr &conn_mgr, int wait_ms) {
#ifdef HT_WITH_THRIFT
    if (properties->has("host"))
      properties->set("thrift-host", properties->get_str("host"));

    if (get_bool("display-address")) {
      std::cout << get_str("thrift-host") << ":" << get_i16("thrift-port")
          << std::endl;
      quick_exit(EXIT_SUCCESS);
    }

    String table_id;
    InetAddr addr(get_str("thrift-host"), get_i16("thrift-port"));
	
    try {
	    Thrift::Transport ttp;
	    if (strcmp(get_str("thrift-transport").c_str(), "zlib") == 0)
		    ttp = Thrift::Transport::ZLIB;
	    else
		    ttp = Thrift::Transport::FRAMED;
      Thrift::Client client(ttp, get_str("thrift-host"), get_i16("thrift-port"));
      ThriftGen::Namespace ns = client.open_namespace("sys");
      client.get_table_id(table_id, ns, "METADATA");
      client.namespace_close(ns);
    }
    catch (ThriftGen::ClientException &e) {
      HT_THROW(e.code, e.message);
    }
    catch (std::exception &e) {
      HT_THROW(Error::EXTERNAL, e.what());
    }
    HT_EXPECT(table_id == TableIdentifier::METADATA_ID, Error::INVALID_METADATA);
#else
    HT_THROW(Error::FAILED_EXPECTATION, "Thrift support not installed");
#endif
  }

} // local namespace

#define CHECK_SERVER(_server_) do { \
  try { check_##_server_(conn_mgr, wait_ms); } catch (Exception &e) { \
    if (verbose) { \
      HT_DEBUG_OUT << e << HT_END; \
      cout << #_server_ <<" - down" << endl; \
    } \
    ++down; \
    break; \
  } \
  if (verbose) cout << #_server_ <<" - up" << endl; \
} while (0)


int main(int argc, char **argv) {
  int down = 0;

  try {
    init_with_policies<Policies>(argc, argv);

    bool silent = has("silent") && get_bool("silent");
    bool verbose = get<gBool>("verbose");
    uint32_t wait_ms = get_i32("wait");
    String server_name = get("server-name", String());

    ConnectionManagerPtr conn_mgr = make_shared<ConnectionManager>();
    conn_mgr->set_quiet_mode(silent);

    properties->set("FsBroker.Timeout", (int32_t)wait_ms);

    if (server_name == "fsbroker") {
      CHECK_SERVER(fsbroker);
    }
    else if (server_name == "hyperspace") {
      CHECK_SERVER(hyperspace);
    }
    else if (server_name == "global-master" || server_name == "global_master") {
      CHECK_SERVER(global_master);
    }
    else if (server_name == "master") {
      CHECK_SERVER(master);
    }
    else if (server_name == "rangeserver") {
      CHECK_SERVER(rangeserver);
    }
    else if (server_name == "thriftbroker") {
      CHECK_SERVER(thriftbroker);
    }
    else {
      CHECK_SERVER(fsbroker);
      CHECK_SERVER(hyperspace);
      //CHECK_SERVER(master);
      CHECK_SERVER(global_master);
      CHECK_SERVER(rangeserver);
#ifdef HT_WITH_THRIFT
      CHECK_SERVER(thriftbroker);
#endif
    }

    if (!silent)
      cout << (down ? "false" : "true") << endl;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    quick_exit(EXIT_FAILURE);
  }
  quick_exit(down);   // ditto
}
