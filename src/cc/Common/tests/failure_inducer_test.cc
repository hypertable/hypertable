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
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

#include <Common/Compat.h>
#include <Common/Logger.h>
#include <Common/Init.h>
#include <Common/Config.h>
#include <Common/FailureInducer.h>
#include <Common/FileUtils.h>
#include <Common/String.h>

#include <iostream>
#include <fstream>

using namespace Hypertable;
using namespace std;

namespace {

const char *required_files[] = {
  //"./failure_inducer_test",
  "./failure_inducer_test.golden",
  0
};

void hit_labels(ofstream &os) {
  HT_MAYBE_FAIL("label-1");
  HT_MAYBE_FAIL("label-2");
  HT_MAYBE_FAIL("label-3");
  HT_MAYBE_FAIL("label-4");
  if (HT_FAILURE_SIGNALLED("label-5")) {
    os << "Failure signalled with label 'label-5'" << endl;
  }
}

void test1(const String &outfile) {
  FailureInducer::instance = new FailureInducer();

  FailureInducer::instance->parse_option("label-1:throw:1");
  FailureInducer::instance->parse_option("label-2:throw(4):2");
  FailureInducer::instance->parse_option("label-3:throw(0x5001B):3");
  FailureInducer::instance->parse_option("label-5:signal:6");

  ofstream os(outfile.c_str());

  for (int ii=0; ii < 10; ++ii) {
    try {
      hit_labels(os);
    }
    catch (Exception &e) {
      os << "Caught exception with code " << e.code() << "-" << e.what() << endl;
    }
  }
}

void test2(const String &outfile) {
  FailureInducer::instance = new FailureInducer();

  FailureInducer::instance->parse_option("label-1:throw:1;label-2:throw(4):2;label-3:throw(0x5001B):3;label-5:signal:6");

  ofstream os(outfile.c_str());

  for (int ii=0; ii < 10; ++ii) {
    try {
      hit_labels(os);
    }
    catch (Exception &e) {
      os << "Caught exception with code " << e.code() << "-" << e.what() << endl;
    }
  }
}

} // namespace

int main(int argc, char *argv[]) {
  Config::init(argc, argv);
  String outfile("failure_inducer_test.out");

  for (int i=0; required_files[i]; i++) {
    if (!FileUtils::exists(required_files[i])) {
      HT_ERRORF("Unable to find '%s'", required_files[i]);
      quick_exit(EXIT_FAILURE);
    }
  }
  test1(outfile);
  String cmd_str = "diff failure_inducer_test.out failure_inducer_test.golden";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  test2(outfile);
  cmd_str = "diff failure_inducer_test.out failure_inducer_test.golden";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);
}
