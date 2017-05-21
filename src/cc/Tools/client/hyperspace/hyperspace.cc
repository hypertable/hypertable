/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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

#include <Hyperspace/Config.h>
#include <Hyperspace/Session.h>
#include <Hyperspace/HsCommandInterpreter.h>
#include <Hyperspace/HsClientState.h>

#include <Tools/Lib/CommandShell.h>

#include <AsyncComm/Comm.h>

#include <Common/FailureInducer.h>
#include <Common/Init.h>

#include <boost/algorithm/string.hpp>
#include <boost/thread/exceptions.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace Hypertable;
using namespace Config;
using namespace Hyperspace;
using namespace std;
using namespace boost;


class SessionHandler : public SessionCallback {
public:
  virtual void jeopardy() { }
  virtual void safe() { }
  virtual void expired() { }
  virtual void disconnected() { }
  virtual void reconnected() { }
};

int main(int argc, char **argv) {
  bool silent {};

  typedef Cons<HyperspaceCommandShellPolicy, DefaultCommPolicy> MyPolicy;

  try {
    Comm *comm;
    CommandInterpreterPtr interp;
    CommandShellPtr shell;
    SessionPtr session_ptr;
    SessionHandler session_handler;

    init_with_policy<MyPolicy>(argc, argv);
    HsClientState::exit_status = 0;
    comm = Comm::instance();

    int32_t timeout = has("timeout") ? get_i32("timeout") : 10000;
    silent = has("silent") && get_bool("silent");

    session_ptr = std::make_shared<Hyperspace::Session>(comm, properties);
    session_ptr->add_callback(&session_handler);

    interp = session_ptr->create_hs_interpreter();
    shell = std::make_shared<CommandShell>("hyperspace", "Hyperspace", interp, properties);
    interp->set_silent(shell->silent());
    interp->set_test_mode(shell->test_mode());

    if (has("induce-failure")) {
      if (FailureInducer::instance == 0)
        FailureInducer::instance = new FailureInducer();
      FailureInducer::instance->parse_option(get_str("induce-failure"));
    }

    if(!session_ptr->wait_for_connection(timeout)) {
      if (!silent)
        cout << "Hyperspace CRITICAL - connect error" << endl;
      quick_exit(2);
    }

    HsClientState::exit_status = shell->run();
    return HsClientState::exit_status;
  }
  catch(Exception &e) {
    if (!silent) {
      cout << "Hyperspace CRITICAL - " << Error::get_text(e.code());
      const char *msg = e.what();
      if (msg && *msg)
        cout << " - " << msg;
      cout << endl;
    }
    quick_exit(2);
  }
  return 0;
}
