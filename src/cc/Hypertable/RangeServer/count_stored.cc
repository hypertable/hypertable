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

#include "Common/Compat.h"
#include <fstream>
#include <iostream>
#include <string>

#include "Common/Init.h"
#include "Common/System.h"
#include "Common/Usage.h"

#include "FsBroker/Lib/Client.h"

#include "Hypertable/Lib/Client.h"
#include "Hypertable/Lib/Key.h"
#include "Hypertable/Lib/KeySpec.h"
#include "Hypertable/Lib/ScanSpec.h"

#include "Config.h"
#include "CellStoreFactory.h"
#include "CellStore.h"
#include "Global.h"


using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

struct MyPolicy : Policy {
  static void init_options() {
    cmdline_desc("Usage: %s [options] <table>\n\n"
      "  This program counts the number of cells that exist in CellStores\n"
      "  for a table.  It does this by reading the Files columns for the\n"
      "  table in the METADATA table to learn of all the CellStores.  It\n"
      "  then does a linear scan of each CellStore file, counting the\n"
      "  number of cells...\nOptions");
    cmdline_hidden_desc().add_options()
      ("table", str(), "name of the table to scan")
      ("table", -1);
  }
};

typedef Cons<MyPolicy, DefaultClientPolicy> AppPolicy;

struct RangeCellStoreInfo {
  String start_row;
  String end_row;
  std::vector<String> cell_stores;
};

struct cell_store_info {
  String start_row;
  String end_row;
  String file;
};

void
fill_cell_store_vector(ClientPtr &client, NamespacePtr &ns, const char *table_name,
                       std::vector<cell_store_info> &file_vector);

} // local namespace


int main(int argc, char **argv) {
  try {
    init_with_policy<AppPolicy>(argc, argv);

    String table_name = get("table", String());

    if (table_name.empty()) {
      HT_ERROR_OUT <<"table name is required"<< HT_END;
      cout << cmdline_desc() << endl;
      return 1;
    }

    bool hit_start = false;
    uint64_t total_count = 0;
    uint64_t store_count = 0;
    
    uint32_t timeout = properties->get_pref<int32_t>(
       {"FsBroker.Timeout", "Hypertable.Request.Timeout"});

    // Create Hypertable client object
    ClientPtr hypertable_client = make_shared<Hypertable::Client>(argv[0]);
    NamespacePtr ns = hypertable_client->open_namespace("/");
    ConnectionManagerPtr conn_mgr = make_shared<ConnectionManager>();
    FsBroker::Lib::ClientPtr dfs = std::make_shared<FsBroker::Lib::Client>(conn_mgr, properties);

    Global::dfs = dfs;

    if (!dfs->wait_for_connection(timeout)) {
      cerr << "error: timed out waiting for FS broker" << endl;
      exit(EXIT_FAILURE);
    }

    Global::memory_tracker = new MemoryTracker(0, 0);

    std::vector<cell_store_info> file_vector;

    fill_cell_store_vector(hypertable_client, ns, table_name.c_str(), file_vector);

    ScanContextPtr scan_context_ptr(new ScanContext());
    Key key;
    ByteString value;

    for (size_t i=0; i<file_vector.size(); i++) {
      /**
       * Open cellStore
       */
      CellStorePtr cell_store_ptr = CellStoreFactory::open(file_vector[i].file, 0, 0);
      CellListScannerPtr scanner;

      hit_start = (file_vector[i].start_row == "") ? true : false;
      store_count = 0;
      scanner = cell_store_ptr->create_scanner(scan_context_ptr.get());

      while (scanner->get(key, value)) {
        if (!hit_start) {
          if (strcmp(key.row, file_vector[i].start_row.c_str()) <= 0) {
            scanner->forward();
            continue;
          }
          hit_start = true;
        }
        if (strcmp(key.row, file_vector[i].end_row.c_str()) > 0)
          break;

        store_count++;
        scanner->forward();
      }

      cout << store_count << "\t" << file_vector[i].file << "["
           << file_vector[i].start_row << ".." << file_vector[i].end_row << "]"
           << endl;
      total_count += store_count;
    }
    cout << total_count << "\tTOTAL" << endl;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
  return 0;
}


namespace {

void
fill_cell_store_vector(ClientPtr &client, NamespacePtr &ns, const char *table_name,
                       std::vector<cell_store_info> &file_vector) {
  TablePtr table_ptr;
  NamespacePtr ns_system;
  TableScannerPtr scanner_ptr;
  ScanSpec scan_spec;
  RowInterval ri;
  Cell cell;
  char start_row[16];
  char end_row[16];
  RangeCellStoreInfo range_cell_store_info;
  cell_store_info cell_store_info;
  String table_id;

  try {
    ns_system = client->open_namespace("sys");
    // Open the 'METADATA' table
    table_ptr = ns_system->open_table("METADATA");
    table_id = ns->get_table_id(table_name);

    // Set up the scan specification
    scan_spec.max_versions = 1;
    sprintf(start_row, "%s:", table_id.c_str());
    ri.start = start_row;
    sprintf(end_row, "%s:%s", table_id.c_str(), Key::END_ROW_MARKER);
    ri.end = end_row;
    scan_spec.row_intervals.push_back(ri);
    scan_spec.columns.clear();
    scan_spec.columns.push_back("Files");
    scan_spec.columns.push_back("StartRow");

    // Create a scanner on the 'METADATA' table
    scanner_ptr.reset( table_ptr->create_scanner(scan_spec) );

  }
  catch (std::exception &e) {
    cerr << "error: " << e.what() << endl;
    exit(EXIT_FAILURE);
  }

  range_cell_store_info.start_row = "";
  range_cell_store_info.end_row = "";
  range_cell_store_info.cell_stores.clear();

  // Iterate through the cells returned by the scanner
  while (scanner_ptr->next(cell)) {
    if (strcmp(cell.row_key, range_cell_store_info.end_row.c_str())) {
      if (range_cell_store_info.end_row != "") {
        const char *end_row_cstr =
            strchr(range_cell_store_info.end_row.c_str(), ':');
        if (end_row_cstr == 0) {
          cerr << "error: mal-formed end row (missing colon) - "
               << range_cell_store_info.end_row << endl;
          exit(EXIT_FAILURE);
        }
        end_row_cstr++;
        cell_store_info.start_row = range_cell_store_info.start_row;
        cell_store_info.end_row = end_row_cstr;
        for (size_t i=0; i<range_cell_store_info.cell_stores.size(); i++) {
          cell_store_info.file = range_cell_store_info.cell_stores[i];
          file_vector.push_back(cell_store_info);
        }
      }
      range_cell_store_info.start_row = "";
      range_cell_store_info.end_row = cell.row_key;
      range_cell_store_info.cell_stores.clear();
    }

    if (!strcmp(cell.column_family, "StartRow"))
      range_cell_store_info.start_row =
          String((const char *)cell.value, cell.value_len);
    else if (!strcmp(cell.column_family, "Files")) {
      String files = String((const char *)cell.value, cell.value_len);
      char *ptr, *save_ptr;
      ptr = strtok_r((char *)files.c_str(), "\n\r;", &save_ptr);
      while (ptr) {
        range_cell_store_info.cell_stores.push_back(ptr);
        ptr = strtok_r(0, "\n\r;", &save_ptr);
      }
    }
    else {
      cerr << "Unexpected column family encountered: '" << cell.column_family
           << endl;
      exit(EXIT_FAILURE);
    }
  }

  if (!range_cell_store_info.cell_stores.empty()) {
    const char *end_row_cstr = strchr(range_cell_store_info.end_row.c_str(),
                                      ':');
    if (end_row_cstr == 0) {
      cerr << "error: mal-formed end row (missing colon) - "
           << range_cell_store_info.end_row << endl;
      exit(EXIT_FAILURE);
    }
    end_row_cstr++;
    cell_store_info.start_row = range_cell_store_info.start_row;
    cell_store_info.end_row = end_row_cstr;
    for (size_t i=0; i<range_cell_store_info.cell_stores.size(); i++) {
      cell_store_info.file = range_cell_store_info.cell_stores[i];
      file_vector.push_back(cell_store_info);
    }
  }
}

} // local namespace
