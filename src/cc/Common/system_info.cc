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

/**
 * @file
 * Command line tool to print information about the system.
 */

#include <Common/Compat.h>
#include <Common/Init.h>
#include <Common/SystemInfo.h>

using namespace std;
using namespace Hypertable;
using namespace Config;

namespace {

struct MyPolicy : Policy {
  static void init_options() {
    cmdline_desc().add_options()
      ("my-ip", "Display the primary IP Address of the host")
      ("install-dir", "Display install directory")
      ("property,p", strs(), "Display the value of the specified property")
      ;
  }
};

typedef Meta::list<MyPolicy, DefaultPolicy> Policies;

void dump_all() {
  cout << system_info_lib_version() << endl;
  cout << System::cpu_info() << endl;
  cout << System::cpu_stat() << endl;
  cout << System::mem_stat() << endl;
  cout << System::disk_stat() << endl;
  cout << System::os_info() << endl;
  cout << System::swap_stat() << endl;
  cout << System::net_info() << endl;
  cout << System::net_stat() << endl;
  cout << System::proc_info() << endl;
  cout << System::proc_stat() << endl;
  cout << System::fs_stat() << endl;
  cout << System::term_info() << endl;
  cout << "{System: install_dir='" << System::install_dir
      << "' exe_name='" << System::exe_name << "'}" << endl;
}

} // local namespace

int main(int ac, char *av[]) {
  init_with_policies<Policies>(ac, av);
  bool has_option = false;

  if (has("my-ip")) {
    cout << System::net_info().primary_addr << endl;
    has_option = true;
  }
  if (has("install-dir")) {
    cout << System::install_dir << endl;
    has_option = true;
  }
  if (has("property")) {
    Strings strs = get_strs("property");

    for (auto &s : strs)
      cout << properties->str(s) << endl;

    has_option = true;
  }
  if (!has_option)
    dump_all();
}
