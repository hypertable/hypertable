/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
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
#include <string>
#include <vector>
#include <iostream>

#include <boost/algorithm/string.hpp>

#include "AsyncComm/Comm.h"
#include "AsyncComm/ConnectionManager.h"
#include "AsyncComm/ReactorFactory.h"

#include "Common/Init.h"
#include "Common/ByteString.h"
#include "Common/HashMap.h"
#include "Common/InetAddr.h"
#include "Common/Logger.h"
#include "Common/System.h"
#include "Common/Usage.h"

#include "DfsBroker/Lib/Client.h"

#include "Hypertable/Lib/LoadDataEscape.h"
#include "Hypertable/Lib/Key.h"

#include "Config.h"
#include "CellStore.h"
#include "CellStoreFactory.h"
#include "CellStoreTrailer.h"
#include "Global.h"

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  struct AppPolicy : Config::Policy {
    static void init_options() {
      cmdline_desc("Usage: %s [options] <filename>\n\n"
        "Dumps the contents of the CellStore contained in the DFS <filename>."
        "\n\nOptions").add_options()
        ("all,a", "Dump everything, including key/value pairs")
        ("compact,c", "Only prints the cellstore name and a status ('ok' or 'corrupt')")
        ("count,c", "Count the number of key/value pairs")
        ("column-id-map", str(), "Column family id to name map, format = <id>=<name>[,<id>=<name>...]")
        ("end-key", str(), "Ignore keys that are greater than <arg>")
        ("start-key", str(), "Ignore keys that are less than or equal to <arg>")
        ("tsv-format", "Output data in TSV format")
        ;
      cmdline_hidden_desc().add_options()("filename", str(), "");
      cmdline_positional_desc().add("filename", -1);
    }
    static void init() {
      if (!has("filename")) {
        HT_ERROR_OUT <<"filename required" << HT_END;
        cout << cmdline_desc() << endl;
        exit(1);
      }
    }
  };

  typedef Meta::list<AppPolicy, DfsClientPolicy, DefaultCommPolicy> Policies;

  typedef hash_map<uint32_t, String> ColumnIdMapT;


} // local namespace


int main(int argc, char **argv) {
  try {
    init_with_policies<Policies>(argc, argv);

    bool dump_compact = has("compact");
    bool dump_all = has("all");
    bool count_keys = has("count");
    String start_key = get("start-key", String());
    String end_key = get("end-key", String());
    bool got_end_key = has("end-key");
    bool hit_start = start_key.empty();
    int timeout = get_i32("timeout");
    String fname = get_str("filename");
    bool tsv_format = has("tsv-format");
    char *column_id_map[256];

    ConnectionManagerPtr conn_mgr = new ConnectionManager();

    DfsBroker::Client *dfs = new DfsBroker::Client(conn_mgr, properties);

    if (!dfs->wait_for_connection(timeout)) {
      cerr << "error: timed out waiting for DFS broker" << endl;
      exit(1);
    }

    Global::dfs = dfs;

    Global::memory_tracker = new MemoryTracker(0, 0);

    if (dump_compact) {
      try {
        CellStorePtr cellstore = CellStoreFactory::open(fname, 0, 0);
        std::cout << fname << ": ok" << std::endl;
      }
      catch (Exception &ex) {
        std::cout << fname << ": corrupt" << std::endl;
        _exit(-1);
      }
      _exit(0);
    }

    memset(column_id_map, 0, 256*sizeof(char *));

    if (has("column-id-map")) {
      char *key, *value;
      int id;
      String str = get_str("column-id-map");
      key = strtok((char *)str.c_str(), ",=");
      if (key) {
        value = strtok(0, ",=");
        id = atoi(key);
        column_id_map[id] = value;
      }
      while (key) {
        key = strtok(0, ",=");
        if (key) {
          value = strtok(0, ",=");
          id = atoi(key);
          column_id_map[id] = value;
        }
      }
    }

    /***
    for (size_t i=0; i<256; i++) {
      if (column_id_map[i])
        cout << i << " = " << column_id_map[i] << endl;
    }
    _exit(0);
    **/

    /**
     * Open cellStore
     */
    CellStorePtr cellstore = CellStoreFactory::open(fname, 0, 0);
    CellListScanner *scanner = 0;

    /**
     * Dump keys
     */
    uint64_t key_count = 0;
    ByteString key, value;
    uint8_t *bsptr;
    size_t bslen;
    char *buf = new char [ 1024 ];
    size_t buf_len = 1024;
    Key key_comps;

    /**
     * Dump keys
     */
    if (tsv_format || dump_all || count_keys) {
      LoadDataEscape row_escaper;
      LoadDataEscape escaper;
      ScanContextPtr scan_ctx(new ScanContext());
      const char *unescaped_buf, *row_unescaped_buf;
      size_t unescaped_len, row_unescaped_len;

      if (tsv_format)
        cout << "#timestamp\trow\tcolumn\tvalue\n";

      scanner = cellstore->create_scanner(scan_ctx);
      while (scanner->get(key_comps, value)) {

        if (!hit_start) {
          if (strcmp(key_comps.row, start_key.c_str()) <= 0) {
            scanner->forward();
            continue;
          }
          hit_start = true;
        }
        if (got_end_key && strcmp(key_comps.row, end_key.c_str()) > 0)
          break;
        if (count_keys)
          key_count++;
        else {
          if (tsv_format) {
            row_escaper.escape(key_comps.row, key_comps.row_len,
                               &row_unescaped_buf, &row_unescaped_len);
            if (column_id_map[key_comps.column_family_code])
              cout << key_comps.timestamp << "\t" << row_unescaped_buf << "\t" << column_id_map[key_comps.column_family_code];
            else
              cout << key_comps.timestamp << "\t" << row_unescaped_buf << "\t" << (unsigned int)key_comps.column_family_code;
            if (key_comps.column_qualifier && *key_comps.column_qualifier) {
              escaper.escape(key_comps.column_qualifier, key_comps.column_qualifier_len,
                             &unescaped_buf, &unescaped_len);
              cout << ":" << unescaped_buf;
            }
            bslen = value.decode_length((const uint8_t **)&bsptr);
            if (bslen >= buf_len) {
              delete [] buf;
              buf_len = bslen + 256;
              buf = new char [ buf_len ];
            }
            memcpy(buf, bsptr, bslen);
            buf[bslen] = 0;
            escaper.escape(buf, bslen, &unescaped_buf, &unescaped_len);
            cout << "\t" << (char *)unescaped_buf << "\n";
          }
          else
            cout << key_comps << endl;
        }
        scanner->forward();
      }
      delete scanner;
    }

    if (tsv_format)
      return 0;

    if (count_keys) {
      cout << key_count << endl;
      return 0;
    }

    /**
     * Dump block index
     */
    cout << endl;
    cout << "BLOCK INDEX:" << endl;
    cellstore->display_block_info();

    /**
     * Dump bloom filter size
     */
    cout << endl;
    cout << "BLOOM FILTER SIZE: "
         << cellstore->bloom_filter_size() << endl;

    /**
     * Dump replaced files
     */
    cout << endl;
    const vector<String> &replaced_files = cellstore->get_replaced_files();
    cout << "REPLACED FILES: " << endl;
    for(size_t ii=0; ii < replaced_files.size(); ++ii)
         cout << replaced_files[ii] << endl;

    /**
     * Dump trailer
     */
    CellStoreTrailer *trailer = cellstore->get_trailer();

    cout << endl;
    cout << "TRAILER:" << endl;
    trailer->display_multiline(cout);

    cout << endl;
    cout << "OTHER:" << endl;
    cout << "split row '" << cellstore->get_split_row() << "'" << endl;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }

  return 0;
}
