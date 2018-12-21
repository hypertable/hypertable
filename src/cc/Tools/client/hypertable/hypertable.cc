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

#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/HqlCommandInterpreter.h>

#include <Tools/Lib/CommandShell.h>

#include <Common/ConsoleOutputSquelcher.h>
#include <Common/Init.h>

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc().add_options()
        ("no-log-sync", boo(false),
         "Don't sync rangeserver commit logs on autoflush")
        ("output-only", "Display status output and exit with status 0")
        ("profile", "Send profiling output to stderr")
        ("namespace", str(""),
         "Automatically use specified namespace when starting")
        ("silent-startup", "Squelch output during startup")
        ;
      alias("no-log-sync", "Hypertable.HqlInterpreter.Mutator.NoLogSync");
    }
  };

}

typedef Meta::list<AppPolicy, CommandShellPolicy, DefaultCommPolicy> Policies;


int main(int argc, char **argv) {
  CommandShellPtr shell;
  CommandInterpreterPtr interp;
  Client *hypertable;
  int status {};
  bool silent {};
  bool output_only {};

  try {
    init_with_policies<Policies>(argc, argv);
    bool profile = has("profile");
    output_only = has("output-only");
    silent = has("silent") && get_bool("silent");

    if (has("silent-startup")) {
      ConsoleOutputSquelcher dummy;
      hypertable = new Hypertable::Client();
    }
    else
      hypertable = new Hypertable::Client();
    interp = make_shared<HqlCommandInterpreter>(hypertable, profile);
    shell = make_shared<CommandShell>("hypertable", "Hypertable", interp, properties);
    shell->set_namespace(get_str("namespace"));
    interp->set_silent(shell->silent());
    interp->set_test_mode(shell->test_mode());

    status = shell->run();
  }
  catch(Exception &e) {
    if (!silent) {
      cout << "Hypertable CRITICAL - " << Error::get_text(e.code());
      const char *msg = e.what();
      if (msg && *msg)
        cout << " - " << msg;
      cout << endl;
    }
    quick_exit(output_only ? 0 : 2);
  }
  quick_exit(status);
}
