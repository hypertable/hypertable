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

#include "CommandInterpreter.h"

#include <ThriftBroker/Config.h>

#include <Tools/Lib/CommandShell.h>

#include <AsyncComm/Config.h>

#include <Common/Error.h>
#include <Common/Init.h>
#include <Common/Properties.h>
#include <Common/ConsoleOutputSquelcher.h>

using namespace Hypertable;
using namespace Tools::client;
using namespace Config;
using namespace std;

namespace {

  const char *usage =
    "\n"
    "Usage: ht_thriftbroker [options] <host>[:<port>]\n\nOptions"
    ;

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
        ("nowait", "Don't wait for certain commands to complete (e.g. shutdown)")
        ("output-only", "Display status output and exit with status 0")
        ;
      cmdline_hidden_desc().add_options()("address", str(), "");
      cmdline_positional_desc().add("address", -1);
    }
    static void init() {
      if (has("address")) {
        Endpoint e = InetAddr::parse_endpoint(get_str("address"));
        properties->set("ThriftBroker.Host", e.host);
        if (e.port)
          properties->set("ThriftBroker.Port", e.port);
      }
    }
  };

  typedef Meta::list<CommandShellPolicy, ThriftClientPolicy,
                     DefaultCommPolicy, AppPolicy> Policies;


}


int main(int argc, char **argv) {
  int error = 1;
  bool silent {};
  bool output_only {};

  try {
    init_with_policies<Policies>(argc, argv);
    ::uint32_t timeout_ms;
    bool nowait = has("nowait");

    output_only = has("output-only");
    silent = has("silent") && get_bool("silent");

    if (has("timeout"))
      timeout_ms = get_i32("timeout");
    else
      timeout_ms = get_i32("Hypertable.Request.Timeout");

    if (properties->has("host"))
      properties->set("thrift-host", properties->get_str("host"));

    string host { get_str("thrift-host") };
    int port { get_i16("thrift-port") };

    Thrift::ClientPtr client;
    {
      ConsoleOutputSquelcher temp;

	  Thrift::Transport ttp;
	  if (strcmp(get_str("thrift-transport").c_str(), "zlib") == 0)
		  ttp = Thrift::Transport::ZLIB;
	  else
		  ttp = Thrift::Transport::FRAMED;
      client = make_shared<Thrift::Client>(ttp, host, port, timeout_ms);
    }
    
    CommandInterpreterPtr interp = make_shared<thriftbroker::CommandInterpreter>(client, nowait);

    CommandShellPtr shell = make_shared<CommandShell>("thriftbroker", "ThriftBroker", interp, properties);

    error = shell->run();
  }
  catch (Exception &e) {
    if (!silent) {
      cout << "ThriftBroker CRITICAL - " << Error::get_text(e.code());
      const char *msg = e.what();
      if (msg && *msg)
        cout << " - " << msg;
      cout << endl;
    }
    quick_exit(output_only ? 0 : 2);
  }
  catch (ThriftGen::ClientException &e) {
    if (!silent) {
      cout << "ThriftBroker CRITICAL - ";
      if (e.__isset.code && e.code != 0)
        cout << Error::get_text(e.code);
      if (e.__isset.message && !e.message.empty())
        cout << " - " << e.message;
      cout << endl;
    }
    quick_exit(output_only ? 0 : 2);
  }
  catch (std::exception &e) {
    if (!silent)
      cout << "ThriftBroker CRITICAL - " << e.what() << endl;
    quick_exit(output_only ? 0 : 2);
  }

  quick_exit(error);
}
