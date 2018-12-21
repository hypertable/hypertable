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

#include <Common/Compat.h>
#include <Common/Logger.h>
#include <Common/Init.h>
#include <Common/PageArenaAllocator.h>
#include <Common/TestUtils.h>

#include <boost/bind.hpp>

#include <deque>
#include <random>

#define BENCH_ALLOC(_label_, _code_, _n_) do { \
  std::default_random_engine engine; \
  std::uniform_real_distribution<> dr; \
  long size; \
  HT_BENCH(_label_, size = (long)(dr(engine) * 120.0) + 4;  _code_, _n_); \
} while (0)

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace boost;
using namespace std;

namespace {

typedef PageArenaAllocator<char *> Alloc;
typedef deque<char *> Cstrs;
typedef deque<char *, Alloc> Astrs;
typedef deque<String> Strings;

void random_loop(int n) {
  BENCH_ALLOC("loop", (void)size, n);
}

void random_malloc_test(Cstrs &v, int n) {
  v.clear();

  BENCH_ALLOC("malloc", char *c = (char *) malloc(size + 1);
    for (int j = 0; j < size; ++j) c[j] = size;
    c[size] = 0;
    v.push_back(c), n);
}

template <class ArenaT, class StrsT>
void
random_pagearena_test(const String &label, ArenaT &arena, StrsT &v, int n) {
  v.clear();

  BENCH_ALLOC(label, char *c = arena.alloc(size + 1);
    for (int j = 0; j < size; ++j) c[j] = size;
    c[size] = 0;
    v.push_back(c), n);

  cout << ThisThread::get_id() <<": "<< label <<": "<< arena << endl;
}

void random_strings_test(Strings &v, int n) {
  v.clear();

  BENCH_ALLOC("strings", String str(size, size); v.push_back(str), n);
}

template <class AllocT>
void assert_same(const Cstrs &v1, const deque<char *, AllocT> &v2) {
  HT_ASSERT(v1.size() == v2.size());

  for (size_t i = 0; i < v1.size(); ++i)
    HT_ASSERT(strcmp(v1[i], v2[i]) == 0);
}

void assert_same(const Cstrs &v1, const Strings &v2) {
  HT_ASSERT(v1.size() == v2.size());

  for (size_t i = 0; i < v1.size(); ++i) {
    HT_ASSERT(v2[i] == v1[i]);
  }
}

void random_test(int n) {
  Cstrs v, v1, v2, v3;
  random_malloc_test(v, n);

  CharArena arena; // default
  random_pagearena_test("arena default", arena, v1, n);
  assert_same(v, v1);
  HT_BENCH1("arena default free", v1.clear(); arena.free(), n);

  CharArena arena2(256); // stress
  random_pagearena_test("arena 256", arena2, v2, n);
  assert_same(v, v2);
  HT_BENCH1("arena 256 free", v2.clear(); arena2.free(), n);

  CharArena arena3(65536); // larger pages
  random_pagearena_test("arena 65536", arena3, v3, n);
  assert_same(v, v3);
  HT_BENCH1("arena 65536 free", v3.clear(); arena3.free(), n);

  CharArena arena4; // default
  {
    Astrs v4 = Astrs(Alloc(arena4));
    random_pagearena_test("arena alloc default", arena4, v4, n);
    assert_same(v, v4);
    HT_BENCH1("arena alloc default clear", v4.clear(), n);
  }
  HT_BENCH1("arena alloc default free", arena4.free(), n);

  Strings v6;
  random_strings_test(v6, n);
  assert_same(v, v6);
  HT_BENCH1("strings clear", v6.clear(), n);

  HT_BENCH1("malloc free", for (auto s : v) free(s), n);
  HT_BENCH1("malloc clear", v.clear(), n);
}

void test_malloc_frag(int n) {
  Cstrs v;
  random_malloc_test(v, n);
  print_proc_stat();
  HT_BENCH1("malloc free", for (auto s : v) free(s), n);
  HT_BENCH1("malloc clear", v.clear(), n);
}

void test_pagearena_frag(int n, int sz) {
  CharArena arena(sz);
  {
    Cstrs v;
    random_pagearena_test(Hypertable::format("arena %d", sz), arena, v, n);
    print_proc_stat();
    HT_BENCH1(Hypertable::format("arena %d clear", sz), v.clear(), n);
  }
  HT_BENCH1(Hypertable::format("arena %d free", sz), arena.free(), n);
}

void test_pagearena_alloc_frag(int n, int sz) {
  CharArena arena(sz);
  {
    Astrs v = Astrs(Alloc(arena));
    random_pagearena_test(Hypertable::format("arena alloc %d", sz), arena, v, n);
    cout << System::proc_stat() << endl;
    HT_BENCH1(Hypertable::format("arena alloc %d clear", sz), v.clear(), n);
  }
  HT_BENCH1(Hypertable::format("arena alloc %d free", sz), arena.free(), n);
}

void test_strings_frag(int n) {
  Strings v;
  random_strings_test(v, n);
  print_proc_stat();
  HT_BENCH1("strings clear", v.clear(), n);
}

struct MyPolicy : Policy {
  static void init_options() {
    cmdline_desc("Usage: %s [Options] [malloc|arena|arena_alloc|"
                 "string]\nOptions").add_options()
      ("page-size,p", i32(8192), "arena page size")
      ;
  }
};

typedef Meta::list<MyPolicy, DefaultTestPolicy> Policies;

} // local namespace

int main(int ac, char *av[]) {
  try {
    init_with_policies<Policies>(ac, av);
    int n = get_i32("num-items");

    random_loop(n);
    print_proc_stat();

    if (has("components")) {
      int pagesize = get_i32("page-size");

      for (const auto &co : get_strs("components")) {
        if (co == "malloc")
          run_test(bind(test_malloc_frag, n), true);
        else if (co == "arena")
          run_test(bind(test_pagearena_frag, n, pagesize), true);
        else if (co == "arena_alloc")
          run_test(bind(test_pagearena_alloc_frag, n, pagesize), true);
        else if (co == "string")
          run_test(bind(test_strings_frag, n), true);
        else
          cout <<"Unknown component: "<< co << endl;
      }
    }
    else
      run_test(bind(random_test, n), true);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    quick_exit(EXIT_FAILURE);
  }
}
