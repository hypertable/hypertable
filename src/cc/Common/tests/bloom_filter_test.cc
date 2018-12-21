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
#include "Common/Init.h"
#include "Common/BloomFilter.h"
#include "Common/BloomFilterWithChecksum.h"
#include "Common/Logger.h"
#include "Common/Stopwatch.h"
#include "Common/MurmurHash.h"
#include "Common/md5.h"

using namespace std;
using namespace Hypertable;
using namespace Config;

namespace {

struct MyPolicy : Policy {
  static void init_options() {
    cmdline_desc("Usage: %s [Options] [<num_items>]\nOptions").add_options()
      ("MurmurHash2", "Test with MurmurHash2 by Austin Appleby")
      ("length", i16(32), "length of test strings")
      ("false-positive,p", f64(0.01), "false positive probability for Bloomfilter")
      ;
    cmdline_hidden_desc().add_options()
      ("items,n", i32(200*K), "number of items")
      ("items", -1);
  }
};

typedef Meta::list<MyPolicy, DefaultPolicy> Policies;

struct Item {
  String data;

  Item(int i, size_t len) {
    char buf[33], fmt[33];
    md5_hex(&i, sizeof(i), buf);
    sprintf(fmt, "%%-%ds", (int)len);
    data = format(fmt, buf);
  }
};

typedef std::vector<Item> Items;

#define MEASURE(_label_, _code_, _n_) do { \
  Stopwatch w; _code_; w.stop(); \
  cout << _label_ <<": "<< (_n_) / w.elapsed() <<"/s" << endl; \
} while (0)

#define TEST_IF(_hash_) do { \
  if (!has_choice || has(#_hash_)) test<_hash_>(#_hash_); \
} while (0)

struct BloomFilterTest {
  bool has_choice;
  double fp_prob;
  Items items;

  BloomFilterTest(int nitems, size_t len) {
    has_choice = has("MurmurHash2");
    fp_prob = get_f64("false-positive");
    double total = 0.;
    nitems *= 2;

    MEASURE("generating items",
      for (int i = 0; i < nitems; ++i) {
        items.push_back(Item(i, len));
        total += items.back().data.length();
      }, nitems);

    cout <<"  average length="<< total / items.size() << endl;
  }

  template <class HashT>
  void test(const String &label) {
    size_t nitems = items.size() / 2;

    /*** Without Checksum ***/

    BasicBloomFilter<HashT> filter(nitems, fp_prob);

    cout << label << endl;

    MEASURE("  insert", for (size_t i = 0; i < nitems; ++i)
      filter.insert(items[i].data), nitems);

    MEASURE("  true positives", for (size_t i = 0; i < nitems; ++i)
      HT_ASSERT(filter.may_contain(items[i].data)), nitems);

    double false_positives = 0.;
    size_t nfalses = items.size() - nitems;

    MEASURE("  false positives",
      for (size_t i = nitems, n = items.size(); i < n; ++i)
        if (filter.may_contain(items[i].data))
          ++false_positives, nfalses);

    cout << "  false positive rate: expected "<< fp_prob <<", got "
         << false_positives / nfalses << endl;

    /*** With Checksum ***/

    BasicBloomFilterWithChecksum<HashT> *filter_with_checksum = new BasicBloomFilterWithChecksum<HashT>(nitems, fp_prob);

    cout << label << " (with checksum)" << endl;

    MEASURE("  insert", for (size_t i = 0; i < nitems; ++i)
      filter_with_checksum->insert(items[i].data), nitems);

    MEASURE("  true positives", for (size_t i = 0; i < nitems; ++i)
      HT_ASSERT(filter_with_checksum->may_contain(items[i].data)), nitems);

    MEASURE("  false positives",
      for (size_t i = nitems, n = items.size(); i < n; ++i)
        if (filter_with_checksum->may_contain(items[i].data))
          ++false_positives, nfalses);

    cout << "  false positive rate: expected "<< fp_prob <<", got "
         << false_positives / nfalses << endl;

    StaticBuffer sbuf;
    filter_with_checksum->serialize(sbuf);

    StaticBuffer serialized_buf(sbuf.size);
    memcpy(serialized_buf.base, sbuf.base, sbuf.size);

    size_t items_estimate = filter_with_checksum->get_items_actual();
    size_t items_actual = filter_with_checksum->get_items_actual();
    int64_t length = filter_with_checksum->get_length_bits();
    size_t num_hashes = filter_with_checksum->get_num_hashes();

    delete filter_with_checksum;

    /*** With Checksum after Deserialization ***/

    filter_with_checksum = new BasicBloomFilterWithChecksum<HashT>(items_estimate, items_actual, length, num_hashes);

    memcpy(filter_with_checksum->base(), serialized_buf.base, serialized_buf.size);

    cout << label << " (with checksum deserialized)" << endl;

    MEASURE("  true positives", for (size_t i = 0; i < nitems; ++i)
      HT_ASSERT(filter_with_checksum->may_contain(items[i].data)), nitems);

    MEASURE("  false positives",
      for (size_t i = nitems, n = items.size(); i < n; ++i)
        if (filter_with_checksum->may_contain(items[i].data))
          ++false_positives, nfalses);

    cout << "  false positive rate: expected "<< fp_prob <<", got "
         << false_positives / nfalses << endl;

    delete filter_with_checksum;

  }

  void run() {
    TEST_IF(MurmurHash2);
  }
};

} // local namespace

int main(int argc, char *argv[]) {
  try {
    init_with_policies<Policies>(argc, argv);

    BloomFilterTest test(get_i32("items"), get_i16("length"));

    test.run();
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
}
