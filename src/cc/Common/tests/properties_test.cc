/** -*- C++ -*-
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
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

#include "Common/Compat.h"
#include "Common/Logger.h"
#include "Common/Properties.h"
#include <iostream>

using namespace Hypertable;
using namespace Property;
using namespace std;

namespace {

enum Mode { MODE_FOO, MODE_BAR };

void basic_test(PropertiesDesc &desc) {
  static const char *argv[] = { "test", "--string", "foo", "--strs", "s1",
      "--i16", "64k", "--i32", "20M", "--i64", "8g", "--strs", "s2",
      "--int64s", "16G", "--int64s", "32G" };
  vector<String> args;
  args.push_back("--f64");      args.push_back("1k");
  args.push_back("--float64s"); args.push_back("1.5");
  args.push_back("--float64s"); args.push_back("1.5K");
  args.push_back("--float64s"); args.push_back("1.5M");
  args.push_back("--float64s"); args.push_back("1.5g");

  Properties props;
  props.parse_args(sizeof(argv) / sizeof(char *), (char **)argv, desc);
  props.parse_args(args, desc);
  //props.set("string_type", (String)"11111 a String Type 111111");
  //cout << "string_type=" << props.get<String>("string_type") << endl;
  
  props.set("mode", MODE_FOO);
  cout << "mode=" << props.str("mode") << endl;
  props.print(cout, true);
}
/*
void notify(const String &s) {
  cout << __func__ <<": "<< s << endl;
}
*/
}

int main(int argc, char *argv[]) {
  try {
    double f64test = 0;
    PropertiesDesc desc;
    desc.add_options()
      ("help,h", "help")
      ("boo", boo(true)->zero_token(), "a boolean arg")
      ("string", str(), "a string arg")  // ->notifier(notify) >> onChange cb
      ("strs", strs(), "a list of strings")
      ("i16", i16(), "a 16-bit integer arg")
      ("i32", i32(), "a 32-bit integer arg")
      ("i64", i64(), "a 64-bit integer arg")
      ("int64s", i64s(), "a list of 64-bit integers")
      ("f64", f64(f64test), "a double arg")
      ("float64s", f64s(), "a list of doubles")
      ;
    Properties props;
    props.parse_args(argc, argv, desc);

    if (props.has("help"))
      cout << desc << endl;

    cout << "f64test=" << f64test << endl;
    /*
    if (!props.has("f64")) {
      cout << "f64 has!=" << f64test << endl;
      props.set("f64", (double)1234.1234);
      cout << "f64=" << props.str("f64") << endl;
    } else
      cout << "f64 has=" << f64test << endl;
    props.get_value_ptr("f64")->set_value((double)5678.9);
    cout << "f64 set=" << props.str("f64") << endl;
    */

    props.print(cout);
    HT_TRY("basic test", basic_test(desc));
  }
  catch (Exception &e) {
    cout << e << endl;
    return 1;
  }
  return 0;
}
