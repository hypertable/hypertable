/**
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

#include "Common/Compat.h"
#include "Common/Init.h"

using namespace Hypertable;
using namespace Config;
  
namespace cfg {
  enum enum_cfg {
    ONE,
    TWO,
    THREE
  };

  int enum_from_string(String idx) {
    if (idx == "one")
      return enum_cfg::ONE;
    if (idx == "two")
      return enum_cfg::TWO;
    if (idx == "three")
      return enum_cfg::THREE;
    return -1;
  }

  String enum_repr(int value) {
    switch(value){
      case enum_cfg::ONE:
        return "one";
      case enum_cfg::TWO:
        return "two";
      case enum_cfg::THREE:
        return "three";
      default:
        return format("undefined cfg_enum: %d", value);
    }
  }
}
namespace {


struct AppPolicy : Policy {
  static void init_options() {
    
    EnumExt an_enum_cfg(cfg::enum_cfg::THREE);
    an_enum_cfg.set_from_string(cfg::enum_from_string).set_repr(cfg::enum_repr);
        
	  cmdline_desc("Usage: %s [Options] [args]\nOptions").add_options()
      ("i16", i16(), "16-bit integer")
      ("i32", i32(), "32-bit integer")
      ("i64", i64(), "64-bit integer")
      ("int64s", i64s(), "a list of 64-bit integers")
      ("boo", boo(false)->zero_token(), "a boolean arg")
      ("string", str(), "a string arg")
      ("strs", strs(), "a list of strings")
      ("float64", f64(), "a double arg")
      ("f64s", f64s(), "a list of doubles")
      ("enum_ext", enum_ext(an_enum_cfg), "a list of doubles")
      ;
	  cmdline_hidden_desc().add_options()
      ("args", strs(), "arguments")
      ("args", -1);
  }
};

typedef Meta::list<AppPolicy, DefaultPolicy> Policies;

} // local namespace

int main(int argc, char *argv[]) {
  init_with_policies<Policies>(argc, argv);
  properties->print(std::cout);
  /*
  std::cout << "boo=" << properties->str("boo") << "\n";
  std::cout << "i16=" << properties->str("i16") << "\n";
  std::cout << "i32=" << properties->str("i32") << "\n";
  std::cout << "i64=" << properties->str("i64") << "\n";
  */
}
