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
 * abig with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

#include <Common/Compat.h>
#include <Common/TestUtils.h>
#include <Common/Init.h>
#include <Common/PageArenaAllocator.h>

#include <deque>
#include <vector>

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace boost;

// Performance tests for various containers used the project
namespace {

typedef PageArenaAllocator<double> Alloc;
typedef std::deque<double, Alloc> Deq;
typedef std::vector<double, Alloc> Vec;

void test_small_deque(int s, int reserve, int n) {
  CharArena arena;
  int l = n / s;

  HT_BENCH1(Hypertable::format("small deque %d", s), for (int i = 0; i < l; ++i) {
    Deq q = Deq(Alloc(arena));
    for (int j = 0; j < s; ++j) q.push_back(1e8); }, n);
}

void test_small_vector(int s, int reserve, int n) {
  CharArena arena;
  int l = n / s;

  HT_BENCH1(Hypertable::format("small vector %d", s), for (int i = 0; i < l; ++i) {
    Vec v = Vec(Alloc(arena)); v.reserve(reserve);
    for (int j = 0; j < s; ++j) v.push_back(1e8); }, n);
}

void test_big_deque(int n) {
  CharArena arena;
  Deq q = Deq(Alloc(arena));

  HT_BENCH("big deque", q.push_back(1e8), n);
}

void test_big_vector(int n) {
  CharArena arena;
  Vec v = Vec(Alloc(arena));

  HT_BENCH("big vector", v.push_back(1e8), n);
}

struct MyPolicy : Config::Policy {
  static void init_options() {
    cmdline_desc().add_options()
      ("small-size,s", i32(10), "size of small containers")
      ("reserve", i32(10), "reserve size of small containers")
      ;
  }
};

typedef Meta::list<MyPolicy, DefaultTestPolicy> Policies;

} // local namespace

int main(int ac, char *av[]) {
  try {
    init_with_policies<Policies>(ac, av);

    int n = get_i32("num-items");
    int s = get_i32("small-size");
    int r = get_i32("reserve");

    for (const auto &co : get_strs("components")) {
      if (co == "smalldeque")
        run_test(bind(test_small_deque, s, r, n), true);
      else if (co == "smallvector")
        run_test(bind(test_small_vector, s, r, n), true);
      else if (co == "bigdeque")
        run_test(bind(test_big_deque, n), true);
      else if (co == "bigvector")
        run_test(bind(test_big_vector, n), true);
      else
        HT_ERROR_OUT <<"Unknown component: "<< co << HT_END;
    }
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    exit(EXIT_FAILURE);
  }
}
