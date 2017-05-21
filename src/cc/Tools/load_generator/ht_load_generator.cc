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

#include "LoadClient.h"
#include "LoadThread.h"
#include "QueryThread.h"
#include "ParallelLoad.h"

#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/DataGenerator.h>
#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/Cells.h>

#include <Common/Stopwatch.h>
#include <Common/String.h>
#include <Common/Init.h>
#include <Common/Usage.h>

#include <boost/algorithm/string.hpp>
#include <boost/progress.hpp>
#include <boost/shared_array.hpp>
#include <boost/thread/thread.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace std;

namespace {

  const char *usage =
    "\n"
    "Usage: ht_generate_load [options] <type>\n\n"
    "Description:\n"
    "  This program is used to generate load on a Hypertable\n"
    "  cluster.  The <type> argument indicates the type of load\n"
    "  to generate ('query' or 'update').\n\n"
    "Options";

  struct AppPolicy : Config::Policy {
    static void init_options() {
      allow_unregistered_options(true);
      cmdline_desc(usage).add_options()
        ("help-config", "Show help message for config properties")
        ("table", str()->default_value("LoadTest"), "Name of table to query/update")
        ("delete-percentage", i32(),
         "When generating update workload make this percentage deletes")
        ("max-bytes", i64(), "Amount of data to generate, measured by number "
         "of key and value bytes produced")
        ("max-keys", i64(), "Maximum number of keys to generate for query load")
        ("parallel", i32()->default_value(0),
         "Spawn threads to execute requests in parallel")
        ("query-delay", i32(), "Delay milliseconds between each query")
        ("query-mode", str(),
         "Whether to query 'index' or 'qualifier' index")
        ("sample-file", str(),
         "Output file to hold request latencies, one per line")
        ("seed", i32()->default_value(1), "Pseudo-random number generator seed")
        ("row-seed", i32()->default_value(1), "Pseudo-random number generator seed")
        ("spec-file", str(),
         "File containing the DataGenerator specification")
        ("stdout", boo()->zero_tokens()->default_value(false),
         "Display generated data to stdout instead of sending load to cluster")
        ("verbose,v", boo()->zero_tokens()->default_value(false),
         "Show more verbose output")
        ("flush", boo()->zero_tokens()->default_value(false), "Flush after each update")
        ("no-log-sync", boo()->zero_tokens()->default_value(false), "Don't sync rangeserver commit logs on autoflush")
        ("no-log", "Don't write to the commit log")
        ("flush-interval", i64()->default_value(0),
         "Amount of data after which to mutator buffers are flushed "
         "and commit log is synced. Only used if no-log-sync flag is on")
        ("shared-mutator-flush-interval", i64()->default_value(0),
         "Created a shared mutator using this value as the flush interval")
        ("thrift", boo()->zero_tokens()->default_value(false),
         "Generate load via Thrift interface instead of C++ client library")
        ("version", "Show version information and exit")
        ("overwrite-delete-flag", str(), "Force delete flag (DELETE_ROW, DELETE_CELL, DELETE_COLUMN_FAMILY)")
        ;
      alias("delete-percentage", "DataGenerator.DeletePercentage");
      alias("max-bytes", "DataGenerator.MaxBytes");
      alias("max-keys", "DataGenerator.MaxKeys");
      alias("seed", "DataGenerator.Seed");
      alias("row-seed", "rowkey.seed");
      cmdline_hidden_desc().add_options()
        ("type", str(), "Type (update or query).");
      cmdline_positional_desc().add("type", 1);
    }
  };
}


typedef Meta::list<AppPolicy, DataGeneratorPolicy, DefaultCommPolicy> Policies;

void 
generate_update_load(PropertiesPtr &props, String &tablename, bool flush,
                     ::uint32_t mutator_flags, ::uint64_t flush_interval,
                     ::uint64_t shared_mutator_flush_interval, bool to_stdout,
                     String &sample_fname, ::int32_t delete_pct, bool thrift);

void 
generate_update_load_parallel(PropertiesPtr &props, String &tablename,
                              ::int32_t parallel, bool flush, ::uint32_t mutator_flags,
                              ::uint64_t flush_interval, 
                              ::uint64_t shared_mutator_flush_interval, 
                              ::int32_t delete_pct, bool thrift);

void generate_query_load(PropertiesPtr &props, String &tablename,
        bool to_stdout, ::int32_t delay, String &sample_fname, bool thrift);

void generate_query_load_parallel(PropertiesPtr &props, String &tablename,
        int32_t parallel);

double std_dev(::uint64_t nn, double sum, double sq_sum);

void parse_command_line(int argc, char **argv, PropertiesPtr &props);


int main(int argc, char **argv) {
  String table, load_type, spec_file, sample_fname;
  PropertiesPtr generator_props = std::make_shared<Properties>();
  bool flush, to_stdout, thrift;
  ::uint64_t flush_interval=0;
  ::uint64_t shared_mutator_flush_interval=0;
  ::int32_t query_delay = 0;
  ::int32_t delete_pct = 0;
  ::int32_t parallel = 0;
  ::uint32_t mutator_flags = 0;

  try {
    init_with_policies<Policies>(argc, argv);

    if (!has("type")) {
      std::cout << cmdline_desc() << std::flush;
      quick_exit(EXIT_SUCCESS);
    }

    load_type = get_str("type");

    table = get_str("table");

    parallel = get_i32("parallel");

    sample_fname = has("sample-file") ? get_str("sample-file") : "";

    if (has("query-delay"))
      query_delay = get_i32("query-delay");

    flush = get_bool("flush");
    if (has("no-log"))
      mutator_flags |= TableMutator::FLAG_NO_LOG;
    else if (get_bool("no-log-sync"))
      mutator_flags |= TableMutator::FLAG_NO_LOG_SYNC;
    to_stdout = get_bool("stdout");
    if (mutator_flags & TableMutator::FLAG_NO_LOG_SYNC)
      flush_interval = get_i64("flush-interval");
    shared_mutator_flush_interval = get_i64("shared-mutator-flush-interval");
    thrift = get_bool("thrift");

    if (has("spec-file")) {
      spec_file = get_str("spec-file");
      if (FileUtils::exists(spec_file))
        generator_props->load(spec_file, cmdline_hidden_desc(), true);
      else
        HT_THROW(Error::FILE_NOT_FOUND, spec_file);
    }

    parse_command_line(argc, argv, generator_props);

    if (generator_props->has("DataGenerator.MaxBytes") &&
        generator_props->has("DataGenerator.MaxKeys")) {
      HT_ERROR("Only one of 'DataGenerator.MaxBytes' or 'DataGenerator.MaxKeys' may be specified");
      quick_exit(EXIT_FAILURE);
    }

    if (generator_props->has("DataGenerator.DeletePercentage"))
      delete_pct = generator_props->get_i32("DataGenerator.DeletePercentage");

    if (load_type == "update" && parallel > 0)
      generate_update_load_parallel(generator_props, table, parallel, flush,
                                    mutator_flags, flush_interval,
                                    shared_mutator_flush_interval, delete_pct,
                                    thrift);
    else if (load_type == "update")
      generate_update_load(generator_props, table, flush, mutator_flags,
                           flush_interval, shared_mutator_flush_interval,
                           to_stdout, sample_fname, delete_pct, thrift);
    else if (load_type == "query") {
      if (!generator_props->has("DataGenerator.MaxKeys")
              && !generator_props->has("max-keys")) {
        HT_ERROR("'DataGenerator.MaxKeys' or --max-keys must be specified for "
                "load type 'query'");
        quick_exit(EXIT_FAILURE);
      }
      if (parallel > 0) {
        if (to_stdout) {
          HT_FATAL("--stdout switch not supported for parallel queries");
          quick_exit(EXIT_FAILURE);
        }
        if (sample_fname != "") {
          HT_FATAL("--sample-file not supported for parallel queries");
          quick_exit(EXIT_FAILURE);
        }
        if (has("query-mode")) {
          HT_FATAL("--query-mode not supported for parallel queries");
          quick_exit(EXIT_FAILURE);
        }
        if (thrift) {
          HT_FATAL("thrift mode not supported for parallel queries");
          quick_exit(EXIT_FAILURE);
        }

        generate_query_load_parallel(generator_props, table, parallel);
      }
      else
        generate_query_load(generator_props, table, to_stdout, query_delay,
                sample_fname, thrift);
    }
    else {
      std::cout << cmdline_desc() << std::flush;
      quick_exit(EXIT_FAILURE);
    }
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    exit(EXIT_FAILURE);
  }

  fflush(stdout);
  quick_exit(EXIT_SUCCESS); // don't bother with static objects
}


void parse_command_line(int argc, char **argv, PropertiesPtr &props) {
  const char *ptr;
  String key, value;
  props->parse_args(argc, argv, cmdline_desc(), 0, 0, true);
  for (int i=1; i<argc; i++) {
    if (argv[i][0] == '-') {
      ptr = strchr(argv[i], '=');
      if (ptr) {
        key = String(argv[i], ptr-argv[i]);
        trim_if(key, is_any_of("-"));
        value = String(ptr+1);
        trim_if(value, is_any_of("'\""));
        if (key == "delete-percentage") {
          props->set(key, boost::any( atoi(value.c_str()) ));
          props->set("DataGenerator.DeletePercentage", boost::any( atoi(value.c_str()) ));
        }
        else if (key == ("max-bytes")) {
          props->set(key, boost::any( strtoll(value.c_str(), 0, 0) ));
          props->set("DataGenerator.MaxBytes", boost::any( strtoll(value.c_str(), 0, 0) ));
        }
        else if (key == ("max-keys")) {
          props->set(key, boost::any( strtoll(value.c_str(), 0, 0) ));
          props->set("DataGenerator.MaxKeys", boost::any( strtoll(value.c_str(), 0, 0) ));
        }
        else if (key == "seed") {
          props->set(key, boost::any( atoi(value.c_str()) ));
          props->set("DataGenerator.Seed", boost::any( atoi(value.c_str()) ));
        }
        else if (key == "row-seed") {
          props->set(key, boost::any( atoi(value.c_str()) ));
          props->set("rowkey.seed", boost::any( atoi(value.c_str()) ));
        }
        else
          props->set(key, boost::any(value));
      }
      else {
        key = String(argv[i]);
        trim_if(key, is_any_of("-"));
        if (!props->has(key))
          props->set(key, boost::any( true ));
      }
    }
  }
}


void
generate_update_load(PropertiesPtr &props, String &tablename, bool flush,
                     ::uint32_t mutator_flags, ::uint64_t flush_interval,
                     ::uint64_t shared_mutator_flush_interval, bool to_stdout,
                     String &sample_fname, ::int32_t delete_pct, bool thrift)
{
  double cum_latency=0, cum_sq_latency=0, latency=0;
  double min_latency=10000000, max_latency=0;
  ::uint64_t total_cells=0;
  Cells cells;
  clock_t start_clocks=0, stop_clocks=0;
  double clocks_per_usec = (double)CLOCKS_PER_SEC / 1000000.0;
  bool output_samples = false;
  ofstream sample_file;
  DataGenerator dg(props);
  ::uint64_t unflushed_data=0;
  ::uint64_t total_bytes = 0;
  ::uint64_t consume_threshold = 0;

  if (to_stdout) {
    cout << "#row\tcolumn\tvalue\n";
    for (DataGenerator::iterator iter = dg.begin(); iter != dg.end(); iter++) {
      if ((*iter).column_qualifier == 0 || *(*iter).column_qualifier == 0) {
        if (delete_pct != 0 && (::random() % 100) < delete_pct)
          cout << (*iter).row_key << "\t" << (*iter).column_family << "\t\tDELETE_CELL\n";
        else
          cout << (*iter).row_key << "\t" << (*iter).column_family
               << "\t" << (const char *)(*iter).value << "\n";
      }
      else {
        if (delete_pct != 0 && (::random() % 100) < delete_pct)
          cout << (*iter).row_key << "\t" << (*iter).column_family << ":"
               << (*iter).column_qualifier << "\t\tDELETE_CELL\n";
        else
          cout << (*iter).row_key << "\t" << (*iter).column_family << ":"
               << (*iter).column_qualifier << "\t" << (const char *)(*iter).value << "\n";
      }
    }
    cout << std::flush;
    return;
  }

  if (sample_fname != "") {
    sample_file.open(sample_fname.c_str());
    output_samples = true;
  }

  Stopwatch stopwatch;

  try {
    LoadClientPtr load_client_ptr;
    String config_file = get_str("config");
    bool key_limit = props->has("DataGenerator.MaxKeys");
    bool largefile_mode = false;
    ::uint32_t adjusted_bytes = 0;

    if (dg.get_max_bytes() > std::numeric_limits< ::uint32_t >::max()) {
      largefile_mode = true;
      adjusted_bytes = (uint32_t)(dg.get_max_bytes() / 1048576LL);
      consume_threshold = 1048576LL;
    }
    else
      adjusted_bytes = dg.get_max_bytes();

    boost::progress_display progress_meter(key_limit ? dg.get_max_keys() : adjusted_bytes);

    if (config_file != "")
      load_client_ptr = std::make_shared<LoadClient>(config_file, thrift);
    else
      load_client_ptr = std::make_shared<LoadClient>(thrift);

    load_client_ptr->create_mutator(tablename, mutator_flags,
                                    shared_mutator_flush_interval);

    unsigned delete_flag = (unsigned)-1;
    if (has("overwrite-delete-flag")) {
      String delete_flag_str = get_str("overwrite-delete-flag");
      if (delete_flag_str == "DELETE_ROW")
        delete_flag = FLAG_DELETE_ROW;
      else if (delete_flag_str == "DELETE_CELL")
        delete_flag = FLAG_DELETE_CELL;
      else if (delete_flag_str == "DELETE_COLUMN_FAMILY")
        delete_flag = FLAG_DELETE_COLUMN_FAMILY;
      else if (delete_flag_str != "")
        HT_FATAL("unknown delete flag");
    }

    for (DataGenerator::iterator iter = dg.begin(); iter != dg.end(); total_bytes+=iter.last_data_size(),++iter) {

      if (delete_pct != 0 && (::random() % 100) < delete_pct) {
        KeySpec key;
        key.flag = FLAG_DELETE_ROW;
        key.row = (*iter).row_key;
        key.row_len = strlen((const char *)key.row);
        key.column_family = (*iter).column_family;
        if (key.column_family != 0)
          key.flag = FLAG_DELETE_COLUMN_FAMILY;
        key.column_qualifier = (*iter).column_qualifier;
        if (key.column_qualifier != 0) {
          key.column_qualifier_len = strlen(key.column_qualifier);
          if (key.column_qualifier_len != 0)
            key.flag = FLAG_DELETE_CELL;
        }
        key.timestamp = (*iter).timestamp;
        key.revision = (*iter).revision;

        if (delete_flag == FLAG_DELETE_ROW) {
          key.column_family = 0;
          key.column_qualifier = 0;
          key.column_qualifier_len = 0;
          key.flag = delete_flag;
        }
        else if (delete_flag == FLAG_DELETE_COLUMN_FAMILY) {
          key.flag = delete_flag;
        }
        else if (delete_flag == FLAG_DELETE_CELL) {
          key.flag = delete_flag;
        }

        if (flush)
          start_clocks = clock();
        load_client_ptr->set_delete(key);
      }
      else {
        // do update
        cells.clear();
        cells.push_back(*iter);
        if (flush)
          start_clocks = clock();
        load_client_ptr->set_cells(cells);
      }

      if (flush) {
        load_client_ptr->flush();
        stop_clocks = clock();
        if (stop_clocks < start_clocks)
          latency = ((std::numeric_limits<clock_t>::max() - start_clocks) + stop_clocks) / clocks_per_usec;
        else
          latency = (stop_clocks-start_clocks) / clocks_per_usec;
        if (output_samples)
          sample_file << (unsigned long)latency << "\n";
        else {
          cum_latency += latency;
          cum_sq_latency += pow(latency,2);
          if (latency < min_latency)
            min_latency = latency;
          if (latency > max_latency)
            max_latency = latency;
        }
      }
      else if (flush_interval>0) {
        // if flush interval was specified then keep track of how much data is currently
        // not flushed and call flush once the flush interval limit is reached
        unflushed_data += iter.last_data_size();
        if (unflushed_data > flush_interval) {
          load_client_ptr->flush();
          unflushed_data = 0;
        }

      }

      ++total_cells;
      if (key_limit)
               progress_meter += 1;
      else {
               if (largefile_mode == true) {
                 if (total_bytes >= consume_threshold) {
                   uint32_t consumed = 1 + (uint32_t)((total_bytes - consume_threshold) / 1048576LL);
                   progress_meter += consumed;
                   consume_threshold += (::uint64_t)consumed * 1048576LL;
                 }
               }
               else
                 progress_meter += iter.last_data_size();
      }
    }

    load_client_ptr->flush();

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    exit(EXIT_FAILURE);
  }


  stopwatch.stop();

  printf("\n");
  printf("\n");
  printf("        Elapsed time: %.2f s\n", stopwatch.elapsed());
  printf("Total cells inserted: %llu\n", (Llu) total_cells);
  printf("Throughput (cells/s): %.2f\n", (double)total_cells/stopwatch.elapsed());
  printf("Total bytes inserted: %llu\n", (Llu)total_bytes);
  printf("Throughput (bytes/s): %.2f\n", (double)total_bytes/stopwatch.elapsed());

  if (flush && !output_samples) {
    printf("  Latency min (usec): %llu\n", (Llu)min_latency);
    printf("  Latency max (usec): %llu\n", (Llu)max_latency);
    printf("  Latency avg (usec): %llu\n", (Llu)((double)cum_latency/total_cells));
    printf("Latency stddev (usec): %llu\n", (Llu)std_dev(total_cells, cum_latency, cum_sq_latency));
  }
  printf("\n");
  fflush(stdout);

  if (output_samples)
    sample_file.close();
}

void
generate_update_load_parallel(PropertiesPtr &props, String &tablename,
                              ::int32_t parallel, bool flush, ::uint32_t mutator_flags,
                              ::uint64_t flush_interval,
                              ::uint64_t shared_mutator_flush_interval,
                              ::int32_t delete_pct, bool thrift)
{
  double cum_latency=0, cum_sq_latency=0;
  double min_latency=0, max_latency=0;
  ::uint64_t total_cells=0;
  ::uint64_t total_bytes=0;
  Cells cells;
  ofstream sample_file;
  DataGenerator dg(props);
  std::vector<ParallelStateRec> load_vector(parallel);
  ::uint32_t next = 0;
  ::uint64_t consume_threshold = 0;
  ::uint64_t consume_total = 0;
  boost::thread_group threads;

  Stopwatch stopwatch;

  try {
    ClientPtr client;
    NamespacePtr ht_namespace;
    TablePtr table;
    LoadClientPtr load_client_ptr;
    String config_file = get_str("config");
    bool key_limit = props->has("DataGenerator.MaxKeys");
    bool largefile_mode = false;
    uint32_t adjusted_bytes = 0;
    LoadRec *lrec;

    client = std::make_shared<Hypertable::Client>(config_file);
    ht_namespace = client->open_namespace("/");
    table = ht_namespace->open_table(tablename);

    for (::int32_t i=0; i<parallel; i++)
      threads.create_thread(LoadThread(table, mutator_flags,
                                       shared_mutator_flush_interval,
                                       load_vector[i]));

    if (dg.get_max_bytes() > std::numeric_limits<long>::max()) {
      largefile_mode = true;
      adjusted_bytes = (uint32_t)(dg.get_max_bytes() / 1048576LL);
      consume_threshold = 1048576LL;
    }
    else
      adjusted_bytes = dg.get_max_bytes();

    boost::progress_display progress_meter(key_limit ?
            dg.get_max_keys() : adjusted_bytes);

    for (DataGenerator::iterator iter = dg.begin(); iter != dg.end();
            total_bytes+=iter.last_data_size(), ++iter) {
      if (delete_pct != 0 && (::random() % 100) < delete_pct)
        lrec = new LoadRec(*iter, true);
      else
        lrec = new LoadRec(*iter);
      lrec->amount = iter.last_data_size();

      {
        std::lock_guard<std::mutex> lock(load_vector[next].mutex);
        load_vector[next].requests.push_back(lrec);
        // Delete garbage, update progress meter
        while (!load_vector[next].garbage.empty()) {
          LoadRec *garbage = load_vector[next].garbage.front();
          if (key_limit)
            progress_meter += 1;
          else {
            if (largefile_mode == true) {
              consume_total += garbage->amount;
              if (consume_total >= consume_threshold) {
                uint32_t consumed = 1 + (uint32_t)((consume_total
                            - consume_threshold) / 1048576LL);
                progress_meter += consumed;
                consume_threshold += (::uint64_t)consumed * 1048576LL;
              }
            }
            else
              progress_meter += garbage->amount;
          }
          delete garbage;
          load_vector[next].garbage.pop_front();
        }
        load_vector[next].cond.notify_all();
      }
      next = (next+1) % parallel;

      ++total_cells;
    }

    for (::int32_t i=0; i<parallel; i++) {
      std::lock_guard<std::mutex> lock(load_vector[i].mutex);
      load_vector[i].finished = true;
      load_vector[i].cond.notify_all();
    }

    threads.join_all();

    min_latency = load_vector[0].min_latency;
    for (::int32_t i=0; i<parallel; i++) {
      cum_latency += load_vector[i].cum_latency;
      cum_sq_latency += load_vector[i].cum_sq_latency;
      if (load_vector[i].min_latency < min_latency)
        min_latency = load_vector[i].min_latency;
      if (load_vector[i].max_latency > max_latency)
        max_latency = load_vector[i].max_latency;
    }
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    exit(EXIT_FAILURE);
  }

  stopwatch.stop();

  printf("\n");
  printf("\n");
  printf("        Elapsed time: %.2f s\n", stopwatch.elapsed());
  printf("Total cells inserted: %llu\n", (Llu) total_cells);
  printf("Throughput (cells/s): %.2f\n", (double)total_cells/stopwatch.elapsed());
  printf("Total bytes inserted: %llu\n", (Llu)total_bytes);
  printf("Throughput (bytes/s): %.2f\n", (double)total_bytes/stopwatch.elapsed());
  if (true) {
  //if (flush) {
    printf("  Latency min (usec): %llu\n", (Llu)min_latency);
    printf("  Latency max (usec): %llu\n", (Llu)max_latency);
    printf("  Latency avg (usec): %llu\n", (Llu)((double)cum_latency/total_cells));
    printf("Latency stddev (usec): %llu\n", (Llu)std_dev(total_cells, cum_latency, cum_sq_latency));
  }

  printf("\n");
  fflush(stdout);

}

enum {
  DEFAULT = 0,
  INDEX = 1,
  QUALIFIER = 2
};

void generate_query_load(PropertiesPtr &props, String &tablename,
        bool to_stdout, ::int32_t delay, String &sample_fname, bool thrift)
{
  double cum_latency=0, cum_sq_latency=0, latency=0;
  double min_latency=10000000, max_latency=0;
  ::int64_t total_cells=0;
  ::int64_t total_bytes=0;
  Cells cells;
  clock_t start_clocks, stop_clocks;
  double clocks_per_usec = (double)CLOCKS_PER_SEC / 1000000.0;
  bool output_samples = false;
  ofstream sample_file;

  int query_mode = DEFAULT;
  if (has("query-mode")) {
    String qm = get_str("query-mode");
    if (qm == "index")
      query_mode = INDEX;
    else if (qm == "qualifier")
      query_mode = QUALIFIER;
    else
      HT_THROW(Error::CONFIG_BAD_VALUE, "invalid query-mode parameter");
  }

  DataGenerator dg(props, query_mode ? false : true);

  if (to_stdout) {
    for (DataGenerator::iterator iter = dg.begin(); iter != dg.end(); iter++) {
      if (query_mode == DEFAULT) {
        if (*(*iter).column_qualifier == 0)
          cout << (*iter).row_key << "\t" << (*iter).column_family << "\n";
        else
          cout << (*iter).row_key << "\t" << (*iter).column_family << ":"
               << (*iter).column_qualifier << "\n";
      }
      else if (query_mode == INDEX) {
        cout << (*iter).row_key << "\t" << (*iter).column_family;
        if (*(*iter).column_qualifier != 0)
          cout << ":" << (*iter).column_qualifier;
        cout << "\t" << (const char *)(*iter).value << "\n";
      }
      else
        cout << "not implemented!\n";
    }
    cout << flush;
    return;
  }

  if (sample_fname != "") {
    sample_file.open(sample_fname.c_str());
    output_samples = true;
  }

  Stopwatch stopwatch;

  try {
    LoadClientPtr load_client_ptr;
    ScanSpecBuilder scan_spec;
    Cell cell;
    String config_file = get_str("config");
    boost::progress_display progress_meter(dg.get_max_keys());
    uint64_t last_bytes = 0;

    if (config_file != "")
      load_client_ptr = std::make_shared<LoadClient>(config_file, thrift);
    else
      load_client_ptr = std::make_shared<LoadClient>(thrift);

    for (DataGenerator::iterator iter = dg.begin(); iter != dg.end(); iter++) {

      if (delay)
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));

      scan_spec.clear();
      if (query_mode == INDEX) {
        scan_spec.add_column_predicate((*iter).column_family,
                                       (*iter).column_qualifier ? (*iter).column_qualifier : "",
                                       ColumnPredicate::QUALIFIER_EXACT_MATCH|ColumnPredicate::EXACT_MATCH,
                                       (const char *)(*iter).value);
      }
      else if (query_mode == QUALIFIER) {
        scan_spec.add_column_predicate((*iter).column_family,
                                       (*iter).column_qualifier ? (*iter).column_qualifier : "",
                                       ColumnPredicate::QUALIFIER_EXACT_MATCH, "");
      }
      else {
        scan_spec.add_column((*iter).column_family);
        scan_spec.add_row((*iter).row_key);
      }

      start_clocks = clock();

      load_client_ptr->create_scanner(tablename, scan_spec.get());
      last_bytes = load_client_ptr->get_all_cells();
      total_bytes += last_bytes;
      load_client_ptr->close_scanner();

      stop_clocks = clock();
      if (stop_clocks < start_clocks)
        latency = ((std::numeric_limits<clock_t>::max() - start_clocks)
                + stop_clocks) / clocks_per_usec;
      else
        latency = (stop_clocks-start_clocks) / clocks_per_usec;
      if (output_samples)
        sample_file << (unsigned long)latency << "\n";
      else {
        cum_latency += latency;
        cum_sq_latency += pow(latency,2);
        if (latency < min_latency)
          min_latency = latency;
        if (latency > max_latency)
          max_latency = latency;
      }

      if (last_bytes)
        ++total_cells;
      progress_meter += 1;
    }
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    exit(EXIT_FAILURE);
  }

  stopwatch.stop();

  printf("\n");
  printf("\n");
  printf("        Elapsed time: %.2f s\n", stopwatch.elapsed());
  printf("Total cells returned: %llu\n", (Llu) total_cells);
  printf("Throughput (cells/s): %.2f\n", (double)total_cells/stopwatch.elapsed());
  printf("Total bytes returned: %llu\n", (Llu)total_bytes);
  printf("Throughput (bytes/s): %.2f\n", (double)total_bytes/stopwatch.elapsed());

  if (!output_samples) {
    printf("  Latency min (usec): %llu\n", (Llu)min_latency);
    printf("  Latency max (usec): %llu\n", (Llu)max_latency);
    printf("  Latency avg (usec): %llu\n", (Llu)((double)cum_latency/total_cells));
    printf("Latency stddev (usec): %llu\n", (Llu)std_dev(total_cells, cum_latency, cum_sq_latency));
  }
  printf("\n");
  fflush(stdout);

  if (output_samples)
    sample_file.close();
}

void generate_query_load_parallel(PropertiesPtr &props, String &tablename,
        int32_t parallel)
{
  double cum_latency = 0, cum_sq_latency = 0, elapsed_time = 0;
  double min_latency = 10000000, max_latency = 0;
  ::int64_t total_cells = 0;
  ::int64_t total_bytes = 0;

  int64_t max_keys = props->has("DataGenerator.MaxKeys")
                        ? props->get_i64("DataGenerator.MaxKeys")
                        : props->get_i64("max-keys");
  boost::progress_display progress(max_keys * parallel);

  String config_file = get_str("config");
  ClientPtr client = std::make_shared<Hypertable::Client>(config_file);
  NamespacePtr ht_namespace = client->open_namespace("/");
  TablePtr table = ht_namespace->open_table(tablename);

  boost::thread_group threads;
  std::vector<ParallelStateRec> load_vector(parallel);
  for (::int32_t i = 0; i < parallel; i++)
    threads.create_thread(QueryThread(props, table, &progress, load_vector[i]));

  // wait for the threads to finish
  threads.join_all();

  // accumulate all the gathered metrics
  min_latency = load_vector[0].min_latency;
  for (::int32_t i = 0; i < parallel; i++) {
    cum_latency += load_vector[i].cum_latency;
    cum_sq_latency += load_vector[i].cum_sq_latency;
    total_cells += load_vector[i].total_cells;
    total_bytes += load_vector[i].total_bytes;
    elapsed_time = load_vector[i].elapsed_time;
    if (load_vector[i].min_latency < min_latency)
      min_latency = load_vector[i].min_latency;
    if (load_vector[i].max_latency > max_latency)
      max_latency = load_vector[i].max_latency;
  }

  printf("\n");
  printf("\n");
  printf("        Elapsed time: %.2f s\n", elapsed_time);
  printf("Total cells returned: %llu\n", (Llu) total_cells);
  printf("Throughput (cells/s): %.2f\n", (double)total_cells / elapsed_time);
  printf("Total bytes returned: %llu\n", (Llu)total_bytes);
  printf("Throughput (bytes/s): %.2f\n", (double)total_bytes / elapsed_time);

  printf("  Latency min (usec): %llu\n", (Llu)min_latency);
  printf("  Latency max (usec): %llu\n", (Llu)max_latency);
  printf("  Latency avg (usec): %llu\n", (Llu)((double)cum_latency
              / total_cells));
  printf("Latency stddev (usec): %llu\n", (Llu)std_dev(total_cells,
              cum_latency, cum_sq_latency));
  printf("\n");
  fflush(stdout);

}


/**
 * @param nn Size of set of numbers
 * @param sum Sum of numbers in set
 * @param sq_sum Sum of squares of numbers in set
 * @return std deviation of set
 */
double std_dev(::uint64_t nn, double sum, double sq_sum)
{
  double mean = sum/nn;
  double sq_std = sqrt((sq_sum/(double)nn) - pow(mean,2));
  return sq_std;
}
