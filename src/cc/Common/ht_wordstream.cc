/*
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

/** @file
 * Application to generate word streams.
 * This small helper application prints word streams to stdout. The words are
 * read from a file (default /usr/share/dict/words), shuffled and concatenated
 * with a space (' ').
 */

#include <Common/Compat.h>
#include <Common/Init.h>
#include <Common/Checksum.h>
#include <Common/FileUtils.h>
#include <Common/WordStream.h>

#include <iostream>

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace std;

namespace {

  const char *usage =
    "\n"
    "Usage: ht_wordstream [options] <words-per-record> [<word-file>]\n\n"
    "Description:\n"
    "  This program is used to generate a stream of records, where each record\n"
    "  constists of one or more randomly chosen words concatenated together with\n"
    "  a ' ' character.  The <words-per-record> argument controls how many words\n"
    "  each record should consist of, and the <word-file> argument can be used to\n"
    "  specify the file containing the source words, this argument defaults to\n"
    "  /usr/share/dict/words.\n\n"
    "Options";

  struct MyPolicy : Config::Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
        ("limit,l", i64(), "Limit on the number of records to output")
        ("seed,s", i32(1), "Pseudo-random number generator seed")
        ;
      cmdline_hidden_desc().add_options()
        ("words-per-record", i32(), "Number of words for each output record.")
        ("word-file", str("/usr/share/dict/words"), "Number of words for each output record.")
         ("words-per-record", 1)
         ("word-file", -1);
    }
  };
}

typedef Meta::list<MyPolicy, DefaultPolicy> Policies;

int main(int argc, char **argv) {
  try {
    init_with_policies<Policies>(argc, argv);

    if (!has("words-per-record")) {
      cout << cmdline_desc() << flush;
      quick_exit(EXIT_SUCCESS);
    }

    WordStreamPtr word_stream
      = make_shared<WordStream>(get_str("word-file"), 1, 2, false);

    while (true)
      cout << word_stream->next() << "\n";
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    quick_exit(EXIT_FAILURE);
  }

  cout << std::flush;
  quick_exit(EXIT_SUCCESS);
}
