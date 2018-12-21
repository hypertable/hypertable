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

namespace {

struct AppPolicy : Config::Policy {
  static void init_options() {
	Config::cmdline_desc("Usage: %s [Options] [args]\nOptions").add_options()
   ("Hypertable.Config.OnFileChange.Reload2, OnFileChange.Reload2", cfg(false),
    "Set Config File Listener for Reloading cfg file on Change")
   ("i16", i16(1), "16-bit integer")
   ("i32", i32(), "32-bit integer")
   ("i64", i64(1), "64-bit integer")
   ("int64s", i64s(), "a list of 64-bit integers")
   ("boo,b", boo(false), "a boolean arg")
   ("string,str,s", str("A Default String Value"), "a string arg")
   ("strs", strs(), "a list of strings")
   ("float64", f64(3.12), "a double arg")
   ("f64s", f64s(), "a list of doubles")
   ("skip1", boo()->zero_token(), "a skipped token")
   ("skip2", "a skipped token")
   ("false", boo(false)->zero_token(), "token defaults to false")
   ("true", boo(true)->zero_token(), "token defaults to true")
   ("is_true", boo(false)->zero_token(), "if set token true")
   //("a.cmd.arg.qouted", str(), "a qouted string arg with spaces") require cmake escaping to testdiff  
   ;

	Config::cmdline_hidden_desc().add_options()
   ("args",   strs(), "rest of arguments")
   ("args",  -1 ) // -1, accumalate unknonw options
   ("app",    str(),  "application filename, zero pos skipped")
   ("app",    0)
   ("arg-1",  str(),  "argument one")
   ("arg-1",  1)
   ;
  }
};

typedef Meta::list<AppPolicy, Config::DefaultPolicy> Policies;

} // local namespace

int main(int argc, char *argv[]) {
  //Config::init_with_policies<Policies>(argc, argv);
  AppPolicy::init_options();
  
  std::cout << String("\nConfig::cmdline_desc()");
  std::cout << Config::cmdline_desc();
  std::cout << String("\nConfig::cmdline_hidden_desc()");
  std::cout << Config::cmdline_hidden_desc();

  Config::Parser prs(argc, argv, Config::cmdline_desc(), Config::cmdline_hidden_desc());
  
  std::cout << String("\n\nMerged New ParserConfig");
  std::cout << *prs.get_cfg();
  std::cout << prs; // Raw(Strings) Parsed
  prs.print_options(std::cout);
  

  // cfg file parse test definitions
  Config::file_desc().add_options()
   ("is_true_bool", boo(false), "a boolean arg")
   ("is_yes_bool", boo(false), "a boolean arg")
   ("is_one_bool", boo(false), "a boolean arg")
   ("is_no_bool", boo(true), "a boolean arg")
   ("is_rest_bool", boo(true), "a boolean arg")

   ("boo", boo(false), "a boolean arg")
   ("i16", i16(1), "16-bit integer")
   ("i32", i32(), "32-bit integer")
   ("i64", i64(1), "64-bit integer")
   ("int64s", i64s(), "a list of 64-bit integers")
   ("string", str("A Default String Value"), "a string arg")
   ("string.qouted", str("A Default String Value"), "a string arg")
   ("strs", strs(), "a list of strings")
   ("float64", f64(3.12), "a double arg")
   ("f64s", f64s(), "a list of doubles")
   ("missing", str("a missing string in cfg file, default applies"), "a string arg")
   ("aGroupOne.arg.1", i16(0), "a group arg")
   ("aGroupOne.arg.2", i16(0), "a group arg")
   ("aGroupOne.arg.3", i16(0), "a group arg")
   ("aGroupOne.arg.4", i16(0), "a group arg")
   ("aGroupOne.arg.5", i16(0), "a group arg")
    ;
    
  std::cout << String("\nConfig::file_desc()");
  std::cout << Config::file_desc();

  std::ifstream in("properties_parser_test.cfg");
  Config::Parser prs_file(in, Config::file_desc());
  
  std::cout << String("\nConfig::file_desc()");
  std::cout << Config::file_desc(); // original configuration (left untouched)

  std::cout << *prs_file.get_cfg();  // configurations options with values
  std::cout << prs_file; // Raw(Strings) Parsed
  prs_file.print_options(std::cout);
  
  Properties props;
  props.set("dummy", true);
  std::cout << props.str("dummy") << "\n";

  props.load_from(prs_file.get_options());

  props.print(std::cout, true);

}