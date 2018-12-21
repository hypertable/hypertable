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

#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/NameIdMapper.h>

#include <Common/Error.h>
#include <Common/InetAddr.h>
#include <Common/Logger.h>
#include <Common/Init.h>
#include <Common/Timer.h>
#include <Common/Usage.h>

#include <boost/progress.hpp>
#include <boost/algorithm/string.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

extern "C" {
#include <netdb.h>
#include <sys/types.h>
#include <signal.h>
}

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  const char *usage =
    "Usage: check_integrity [options]\n\n"
    "Description:\n"
    "  This program checks the integrity of the cluster by scanning all \n"
    "  available ranges of all tables.\n"
    "\n"
    "Options\n"
    "  sleep: specifies a sleep time (in seconds) during the requests to\n"
    "  reduce the load that is caused by this tool.\n";

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
          ("sleep", i32(0),
           "Time to sleep between each request (in milliseconds)")
          ("timeout", i32(0),
           "Hypertable connection timeout (in milliseconds)")
          ;
    }

    static void init() {
      // we want to override the default behavior that verbose
      // turns on debugging by clearing the defaulted flag
      if (defaulted("logging-level"))
        properties->set("logging-level", gEnumExt(Logger::Priority::FATAL));
    }
  };

  typedef Meta::list<AppPolicy, FsClientPolicy, HyperspaceClientPolicy,
          MasterClientPolicy, RangeServerClientPolicy, DefaultCommPolicy>
          Policies;

} // global namespace

struct RangeInfo
{
  String tableid;
  String endrow;
};

typedef std::vector<RangeInfo> RangeInfoVec;

std::vector<String> errors;
std::mutex error_mutex;

void add_error(const String &msg)
{
  cout << msg << endl;
  lock_guard<mutex> lock(error_mutex);
  errors.push_back(msg);
}

class LocationThread
{
  public:
    LocationThread(NamespacePtr &ns, NameIdMapperPtr namemap,
            const String &location, boost::progress_display *progress,
            const RangeInfoVec &ranges, uint32_t sleep_ms)
      : m_namespace(ns), m_namemap(namemap), m_ranges(ranges),
        m_progress(progress), m_sleep_ms(sleep_ms) {
    }

    void operator()() {
      TablePtr table;

      for (size_t i = 0; i < m_ranges.size(); i++) {
        String previous_table;
        String previous_end;
        String table_name;

        if (!m_namemap->id_to_name(m_ranges[i].tableid, table_name)) {
          add_error(format("Table id %s is unknown, skipping table",
                      m_ranges[i].tableid.c_str()));
          continue;
        }

        if (table_name != previous_table) {
          previous_table = table_name;
          previous_end = "";
          table = m_namespace->open_table(table_name);
        }

        ScanSpecBuilder ssb;
        ssb.set_cell_limit(1);

        // if the table has only one range then just create a scanner
        // without any row interval
        if (previous_end.empty() && m_ranges[i].endrow == "\xff\xff") {
          // nop
        }
        // otherwise if this range is a normal range then query the end row
        else if (m_ranges[i].endrow != "\xff\xff") {
          ssb.add_row_interval(m_ranges[i].endrow.c_str(), true,
                  m_ranges[i].endrow.c_str(), true);
        }
        // otherwise if this range is the last range of a table then use
        // the previous end row as the start row
        else {
          HT_ASSERT(previous_end.size());
          HT_ASSERT(m_ranges[i].endrow == "\xff\xff");
          ssb.add_row_interval(previous_end.c_str(), false,
                  m_ranges[i].endrow.c_str(), true);
        }

        TableScanner *scanner = table->create_scanner(ssb.get(), 0);
        Cell cell;
        bool found = false;
        while (scanner->next(cell))
         found = true;
        delete scanner;

        (*m_progress) += 1;
        // ignore error if the table is empty
        if (!found
            && (previous_end != ""
              || m_ranges[i].endrow != "\xff\xff")) {
          add_error(format("Table %s: Range [%s..%s] was not found",
                      table_name.c_str(), previous_end.c_str(),
                      m_ranges[i].endrow.c_str()));
          continue;
        }
        if (m_sleep_ms)
          this_thread::sleep_for(chrono::milliseconds(m_sleep_ms));

        previous_end = m_ranges[i].endrow;
      }
    };

  private:
    NamespacePtr m_namespace;
    NameIdMapperPtr m_namemap;
    RangeInfoVec m_ranges;
    boost::progress_display *m_progress;
    uint32_t m_sleep_ms;
};

int main(int argc, char **argv) {
  typedef std::map<String, RangeInfoVec> RangeMap;
  RangeMap ranges;
  try {
    init_with_policies<Policies>(argc, argv);

    uint32_t sleep_ms = get_i32("sleep");
    uint32_t timeout_ms = get_i32("timeout");
    String config = get("config", String(""));

    // connect to the server
    Client *client = new Hypertable::Client(config, timeout_ms);
    NamespacePtr ns = client->open_namespace("/");
    TablePtr metatable = ns->open_table("sys/METADATA");

    // get a list of all ranges and their locations
    int total_ranges = 0;
    ScanSpecBuilder ssb;
    ssb.set_max_versions(1);
    ssb.add_column("Location");
    TableScanner *scanner = metatable->create_scanner(ssb.get(), timeout_ms);
    Cell cell;
    while (scanner->next(cell)) {
      char *endrow = strstr((char *)cell.row_key, ":");
      *endrow++ = 0;
      String location((const char *)cell.value,
              (const char *)cell.value + cell.value_len);
      //cout << cell.row_key << " " << endrow << "\t" << location << endl;

      RangeInfo ri;
      ri.tableid = cell.row_key;
      ri.endrow = endrow;
      ranges[location].push_back(ri);

      total_ranges++;
    }
    delete scanner;

    // initialize a boost progress bar
    cout << "Checking " << total_ranges << " ranges... please wait..." << endl;
    boost::progress_display progress(total_ranges);

    // for each location: start a thread which will check the ranges
    boost::thread_group threads;
    for (RangeMap::iterator it = ranges.begin(); it != ranges.end(); ++it)
      threads.create_thread(LocationThread(ns, client->get_nameid_mapper(),
                  it->first, &progress, it->second, sleep_ms));

    // then wait for the threads to join
    threads.join_all();
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    add_error(format("Exception caught: %s (%d)", Error::get_text(e.code()),
                e.code()));
  }
  
  if (errors.size()) {
    cout << endl << endl << "Got " << errors.size() << " errors: " << endl;
    for (const auto &s : errors)
      cout << "   " << s << endl;
  }
  else
    cout << "Success - all ranges are available." << endl;

  quick_exit(errors.size());   // ditto
}
