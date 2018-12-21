/** -*- c++ -*-
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

#include "Common/Compat.h"
#include "Common/Init.h"

#include "AsyncComm/Config.h"

#include <iostream>

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  String lookup_property;

  const char *usage =
    "\n"
    "Usage: get_property <property> [options]\n\nOptions"
    ;

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc(usage);
      cmdline_hidden_desc().add_options()
      ("property", str(), "")
      ("property", 1);  
      // -1 is an issue to use as a property 
      // if unknown args passed along it will pollute the 'property' arg
      // as well it might be a case of  a name-arg recognition and not applied as any args(-1) 
      // and more right to have one arg to be 1st arg
    }
    static void init() {
      if (has("property"))
        lookup_property = get_str("property");
    }
  };

  typedef Cons<AppPolicy, DefaultCommPolicy> Policies;

} // local namespace

#define TRY(exp, t)        \
    try { cout << exp; goto bail; } catch (Exception &e) { if (t) throw e; }

int main(int argc, char **argv)
{
  try {

    init_with_policy<Policies>(argc, argv);

    if (properties->has(lookup_property))
      TRY(properties->str(lookup_property), true)
    else 
      cout << lookup_property << "-PROPERTY-DOES-NOT-EXIST";
    
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
  }

bail:
  cout << flush;
  quick_exit(EXIT_SUCCESS);
}
