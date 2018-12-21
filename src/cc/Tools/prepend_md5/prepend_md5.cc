/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License.
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

#include <AsyncComm/Config.h>

#include <Common/DynamicBuffer.h>
#include <Common/Init.h>
#include <Common/Logger.h>
#include <Common/md5.h>
#include <Common/SystemInfo.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace std;

namespace {

  const char *usage =
    "\nusage: prepend_md5\n\n"
    "description:\n"
    "  This program reads the first field (delimited by '\\t') of each line\n"
    "  computes the MD5 checksum of the field and then outputs the line with\n"
    "  the checksum prepended to the line\n"
    "\n"
    "options";

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
        ("md5-chars", i32(4), "Name of checksum characters to output")
        ;
    }
    static void init() {
    }
  };

  typedef Meta::list<AppPolicy, DefaultCommPolicy> Policies;

}

/**
 *
 */
int main(int argc, char **argv) {
  char *ptr;
  char hexdigits[33];

  char *line_buffer = new char [ 1024 * 1024 ];

  ios::sync_with_stdio(false);

  try {
    init_with_policies<Policies>(argc, argv);

    cout << System::cpu_info() << endl;
    cout << System::os_info() << endl;
    cout << System::net_info() << endl;
    cout << System::mem_stat() << endl;
    cout << System::fs_stat() << endl;
    exit(0);

    int32_t checksum_chars = get_i32("md5-chars");
    if (checksum_chars < 1 || checksum_chars > 32) {
      cerr << "Invalid value for md5-chars, must be from 1 to 32" << endl;
      exit(EXIT_FAILURE);
    }

    while (!cin.eof()) {

      cin.getline(line_buffer, 1024*1024);

      if (*line_buffer == 0)
	continue;

      ptr = strchr(line_buffer, '\t');
      if (ptr)
	*ptr = 0;

      md5_string(line_buffer, hexdigits);
      hexdigits[checksum_chars] = 0;

      if (ptr)
	*ptr = '\t';

      cout << hexdigits << " " << line_buffer << "\n";
    }
    cout << flush;
  }
  catch (std::exception &e) {
    cerr << "Error - " << e.what() << endl;
    exit(EXIT_FAILURE);
  }
}
