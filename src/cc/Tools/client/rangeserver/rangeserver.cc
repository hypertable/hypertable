/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
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

#include "RangeServerCommandInterpreter.h"

#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/HqlCommandInterpreter.h>
#include <Hypertable/Lib/RangeServer/Client.h>

#include <Hyperspace/Session.h>

#include <Tools/Lib/CommandShell.h>

#include <AsyncComm/DispatchHandler.h>

#include <Common/Init.h>
#include <Common/InetAddr.h>

#include <chrono>
#include <iostream>
#include <thread>

using namespace Hypertable;
using namespace Hypertable::Lib;
using namespace Config;
using namespace Tools::client::rangeserver;
using namespace std;

namespace {

  const char *usage =
    "\n"
    "Usage: ht_rangeserver [options] <host>[:<port>]\n\nOptions"
    ;

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
        ("no-hyperspace", "Do not establish a connection to hyperspace")
        ;
      cmdline_hidden_desc().add_options()
      ("address", str(), "")
      ("address", -1);
    }
    static void init() {
      if (has("address")) {
        Endpoint e = InetAddr::parse_endpoint(get_str("address"));
        properties->set("rs-host", e.host);
        if (e.port) properties->set("rs-port", e.port);
      }
    }
  };

  typedef Meta::list<CommandShellPolicy, HyperspaceClientPolicy,
                     RangeServerClientPolicy, DefaultCommPolicy, AppPolicy> Policies;

  class RangeServerDispatchHandler : public DispatchHandler {
  public:
    RangeServerDispatchHandler(bool silent) : m_silent(silent) { }
    virtual void handle(EventPtr &event_ptr) {
      if (event_ptr->type == Event::DISCONNECT) {
        if (!m_connected) {
          if (!m_silent)
            cout << "RangeServer CRITICAL - connect error" << endl;
          quick_exit(2);
        }
      }
      else if (event_ptr->type == Event::CONNECTION_ESTABLISHED)
        m_connected = true;
    }
  private:
    bool m_silent {};
    bool m_connected {};
  };

} // local namespace


int main(int argc, char **argv) {
  bool silent {};
  int error = 1;
  bool no_hyperspace = false;

  try {
    init_with_policies<Policies>(argc, argv);

    int timeout = get_i32("timeout");
    InetAddr addr(get_str("rs-host"), get_i16("rs-port"));
    silent = has("silent") && get_bool("silent");

    if (has("no-hyperspace"))
      no_hyperspace = true;

    Comm *comm = Comm::instance();

    // Create Range Server client object
    RangeServer::ClientPtr client = make_shared<RangeServer::Client>(comm, timeout);

    DispatchHandlerPtr dispatch_handler_ptr = make_shared<RangeServerDispatchHandler>(silent);
    // connect to RangeServer
    if ((error = comm->connect(addr, dispatch_handler_ptr)) != Error::OK) {
      if (!silent)
        cout << "RangeServer CRITICAL - connect error" << endl;
      quick_exit(2);
    }

    this_thread::sleep_for(chrono::milliseconds(100));

    // Maybe connect to Hyperspace
    Hyperspace::SessionPtr hyperspace;
    if (!no_hyperspace) {
      hyperspace = make_shared<Hyperspace::Session>(comm, properties);
      if (!hyperspace->wait_for_connection(timeout)) {
        if (!silent)
          cout << "RangeServer CRITICAL - Unable to connecto to Hyperspace" << endl;
        quick_exit(2);
      }
    }

    CommandInterpreterPtr interp =
      make_shared<RangeServerCommandInterpreter>(hyperspace, addr, client);

    CommandShellPtr shell = make_shared<CommandShell>("rangeserver", "RangeServer", interp, properties);

    error = shell->run();
  }
  catch (Exception &e) {
    if (!silent) {
      cout << "RangeServer CRITICAL - " << Error::get_text(e.code());
      const char *msg = e.what();
      if (msg && *msg)
        cout << " - " << msg;
      cout << endl;
    }
    quick_exit(2);
  }
  quick_exit(error);
}
