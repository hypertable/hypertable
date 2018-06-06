/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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

#include "Context.h"
#include "Monitoring.h"
#include "RangeServerConnection.h"

#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/Logger.h>
#include <Common/Path.h>
#include <Common/md5.h>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>

extern "C" {
#include <unistd.h>
}

using namespace Hypertable;
using namespace std;

Monitoring::Monitoring(Context *context)
  : m_context(context), m_last_server_count(0), m_disable_rrdtool(false) {
  PropertiesPtr &props = m_context->props;

  /** Create directories for storing monitoring stats */
  m_disable_rrdtool = props->get_bool("Hypertable.Monitoring.Disable");
  m_monitoring_interval = props->get_i32("Hypertable.Monitoring.Interval");
  Path data_dir = props->get_str("Hypertable.DataDirectory");
  m_monitoring_dir = (data_dir /= "/run/monitoring").string();
  m_monitoring_table_dir = m_monitoring_dir + "/tables";
  m_monitoring_rs_dir = m_monitoring_dir + "/rangeservers";

  create_dir(m_monitoring_dir);
  create_dir(m_monitoring_table_dir);
  create_dir(m_monitoring_rs_dir);
  m_allowable_skew = props->get_i32("Hypertable.RangeServer.ClockSkew.Max");
  m_namemap_ptr = m_context->namemap;

  memset(m_last_server_set_digest, 0, 16);
}

void Monitoring::create_dir(const String &dir) {
  if (!FileUtils::exists(dir)) {
    if (!FileUtils::mkdirs(dir)) {
      HT_THROW(Error::LOCAL_IO_ERROR, "Unable to create monitoring dir "+dir);
    }
    HT_INFOF("Created monitoring dir %s",dir.c_str());
  }
  else
    HT_INFOF("rangeservers monitoring stats dir %s exists ",dir.c_str());
}

void Monitoring::add_server(const String &location, const StatsSystem &system_info) {
  lock_guard<mutex> lock(m_mutex);

  RangeServerMap::iterator iter = m_server_map.find(location);

  if (iter != m_server_map.end()) {
    (*iter).second->system_info = make_shared<StatsSystem>(system_info);
    return;
  }

  m_server_map[location] = new RangeServerStatistics();
  m_server_map[location]->location = location;
  m_server_map[location]->system_info = make_shared<StatsSystem>(system_info);
}


void Monitoring::drop_server(const String &location) {
  lock_guard<mutex> lock(m_mutex);

  RangeServerMap::iterator iter = m_server_map.find(location);
  if (iter != m_server_map.end())
    m_server_map.erase(iter);
}

namespace {
  /** STL Strict Weak Ordering for RangeServerStatistics  */
  struct LtRangeServerStatistics {
    bool operator()(const RangeServerStatistics &s1, const RangeServerStatistics &s2) const {
      if (boost::algorithm::starts_with(s1.location, "rs") &&
          boost::algorithm::starts_with(s2.location, "rs")) {
        int id1 = atoi(s1.location.c_str()+2);
        int id2 = atoi(s2.location.c_str()+2);
        return id1 < id2;
      }
      return s1.location < s2.location;
    }
  };
}

void Monitoring::add(std::vector<RangeServerStatistics> &stats) {
  lock_guard<mutex> lock(m_mutex);
  struct rangeserver_rrd_data rrd_data;
  RangeServerMap::iterator iter;
  double numerator, denominator;
  int32_t server_count = 0;
  CstrSet server_set;

  // to keep track max timestamp across rangeserver
  // this value is used to update table rrds
  table_stats_timestamp = 0;
  // copy to previous hashmap to calculate read rates
  m_prev_table_stat_map = m_table_stat_map;
  m_table_stat_map.clear(); // clear the previous contents

  for (size_t i = 0; i < stats.size(); i++) {
    memset(&rrd_data, 0, sizeof(rrd_data));

    iter = m_server_map.find(stats[i].location);
    if (iter == m_server_map.end()) {
      HT_ERRORF("Statistics received for '%s' but not registered for "
              "Monitoring", stats[i].location.c_str());
      continue;
    }

    if (stats[i].fetch_error != Error::OK) {
      (*iter).second->fetch_timestamp = stats[i].fetch_timestamp;
      (*iter).second->fetch_error = stats[i].fetch_error;
      // if server is getting recovered: overwrite the error
      RangeServerConnectionPtr rsc;
      if (stats[i].fetch_error == Error::NO_RESPONSE
          && m_context->rsc_manager->find_server_by_location(stats[i].location, rsc)) {
        if (rsc->is_recovering())
          (*iter).second->fetch_error_msg = "Recovering...";
        else
          (*iter).second->fetch_error_msg = stats[i].fetch_error_msg;
        continue;
      }
    }
    else {
      server_count++;
      server_set.insert(stats[i].location.c_str());
    }

    if ((*iter).second->stats) {

      if (stats[i].stats->query_cache_accesses > (*iter).second->stats->query_cache_accesses) {
        numerator = (double)stats[i].stats->query_cache_hits -
          (double)(*iter).second->stats->query_cache_hits;
        denominator = (double)stats[i].stats->query_cache_accesses -
          (double)(*iter).second->stats->query_cache_accesses;
        rrd_data.qcache_hit_pct = (numerator/denominator)*100.0;
      }

      if (stats[i].stats->block_cache_accesses > (*iter).second->stats->block_cache_accesses) {
        numerator = (double)stats[i].stats->block_cache_hits -
          (double)(*iter).second->stats->block_cache_hits;
        denominator = (double)stats[i].stats->block_cache_accesses -
          (double)(*iter).second->stats->block_cache_accesses;
        rrd_data.bcache_hit_pct = (numerator/denominator)*100.0;
      }

      double elapsed_time = (double)(stats[i].fetch_timestamp - (*iter).second->fetch_timestamp)/1000000000.0;

      rrd_data.scan_rate = stats[i].stats->scan_count/elapsed_time;
      rrd_data.update_rate = stats[i].stats->update_count /elapsed_time;
      rrd_data.sync_rate = stats[i].stats->sync_count/elapsed_time;
      rrd_data.cell_read_rate = stats[i].stats->scanned_cells /elapsed_time;
      rrd_data.cell_write_rate = stats[i].stats->updated_cells /elapsed_time;
      rrd_data.byte_read_rate = stats[i].stats->scanned_bytes /elapsed_time;
      rrd_data.byte_write_rate = stats[i].stats->updated_bytes /elapsed_time;
    }

    rrd_data.timestamp = stats[i].stats_timestamp / 1000000000LL;
    rrd_data.range_count = stats[i].stats->range_count;
    rrd_data.scanner_count = stats[i].stats->scanner_count;
    rrd_data.file_count = stats[i].stats->file_count;
    rrd_data.qcache_max_mem = stats[i].stats->query_cache_max_memory;
    rrd_data.qcache_fill = stats[i].stats->query_cache_max_memory -
      stats[i].stats->query_cache_available_memory;
    rrd_data.bcache_max_mem = stats[i].stats->block_cache_max_memory;
    rrd_data.bcache_fill = stats[i].stats->block_cache_max_memory -
      stats[i].stats->block_cache_available_memory;

    numerator = denominator = 0.0;
    for (size_t j=0; j<stats[i].stats->system.fs_stat.size(); j++) {
      numerator += stats[i].stats->system.fs_stat[j].total 
        - stats[i].stats->system.fs_stat[j].avail;
      denominator += stats[i].stats->system.fs_stat[j].total;
    }
    if (denominator != 0.0)
      rrd_data.disk_used_pct = (numerator/denominator)*100.0;

    for (size_t j=0; j<stats[i].stats->system.disk_stat.size(); j++) {
      rrd_data.disk_read_bytes += (int64_t)stats[i].stats->system.disk_stat[j].read_rate;
      rrd_data.disk_write_bytes += (int64_t)stats[i].stats->system.disk_stat[j].write_rate;
      rrd_data.disk_read_iops += (int64_t)stats[i].stats->system.disk_stat[j].reads_rate;
      rrd_data.disk_write_iops += (int64_t)stats[i].stats->system.disk_stat[j].writes_rate;
    }

    rrd_data.vm_size = (int64_t)stats[i].stats->system.proc_stat.vm_size * 1024*1024;
    rrd_data.vm_resident = (int64_t)stats[i].stats->system.proc_stat.vm_resident * 1024*1024;
    rrd_data.page_in = (int64_t)stats[i].stats->system.swap_stat.page_in;
    rrd_data.page_out = (int64_t)stats[i].stats->system.swap_stat.page_out;
    rrd_data.heap_size = (int64_t)stats[i].stats->system.proc_stat.heap_size;
    rrd_data.heap_slack = (int64_t)stats[i].stats->system.proc_stat.heap_slack;
    rrd_data.tracked_memory = (int64_t)stats[i].stats->tracked_memory;
    rrd_data.net_rx_rate = (int64_t)stats[i].stats->system.net_stat.rx_rate;
    rrd_data.net_tx_rate = (int64_t)stats[i].stats->system.net_stat.tx_rate;
    rrd_data.load_average = stats[i].stats->system.loadavg_stat.loadavg[0];
    rrd_data.cpu_user = stats[i].stats->cpu_user;
    rrd_data.cpu_sys = stats[i].stats->cpu_sys;

    compute_clock_skew(stats[i].stats->timestamp, &stats[i]);

    String rrd_file = m_monitoring_rs_dir + "/" + stats[i].location + "_stats_v0.rrd";

    if (!FileUtils::exists(rrd_file))
      create_rangeserver_rrd(rrd_file);

    if (rrd_data.timestamp > table_stats_timestamp) {
      table_stats_timestamp = rrd_data.timestamp;
    }
    update_rangeserver_rrd(rrd_file, rrd_data);
    add_table_stats(stats[i].stats->tables,stats[i].fetch_timestamp);

    (*iter).second->stats = stats[i].stats;
    (*iter).second->fetch_error = stats[i].fetch_error;
    (*iter).second->fetch_error_msg = stats[i].fetch_error_msg;
    (*iter).second->fetch_timestamp = stats[i].fetch_timestamp;

  }

  // Calculate "server set" MD5 digest
  md5_context md5_ctx;
  md5_starts(&md5_ctx);
  for (auto server : server_set)
    md5_update(&md5_ctx, (const unsigned char *)server, strlen(server));
  unsigned char server_set_digest[16];
  md5_finish(&md5_ctx, server_set_digest);

  // calcualte read rates from previous table stats map

  // finish compression ratio aggregation
  for (TableStatMap::iterator iter = m_table_stat_map.begin();
       iter != m_table_stat_map.end(); ++iter) {
    if (iter->second.disk_used != 0)
      iter->second.compression_ratio =
	(double)iter->second.disk_used / iter->second.compression_ratio;
    else
      iter->second.compression_ratio = 1.0;
    if (iter->second.cell_count != 0) {
      iter->second.average_key_size /= iter->second.cell_count;
      iter->second.average_value_size /= iter->second.cell_count;
    }
    else {
      iter->second.average_key_size = 0.0;
      iter->second.average_value_size = 0.0;
    }
  }

  // Dump RangeServer summary data
  std::vector<RangeServerStatistics> stats_vec;
  struct LtRangeServerStatistics comp;
  stats_vec.reserve(m_server_map.size());
  for (iter = m_server_map.begin(); iter != m_server_map.end(); ++iter)
    stats_vec.push_back(*(*iter).second);
  sort(stats_vec.begin(), stats_vec.end(), comp);
  dump_rangeserver_summary_json(stats_vec);

  // create Table rrd data
  TableStatMap::iterator ts_iter;
  TableStatMap::iterator prev_iter;
  for(ts_iter = m_table_stat_map.begin();ts_iter != m_table_stat_map.end(); ++ts_iter) {

    // calculate read rates and write rates
    prev_iter = m_prev_table_stat_map.find(ts_iter->first);
    if (prev_iter != m_prev_table_stat_map.end()) {
      if (server_count != m_last_server_count ||
          memcmp(m_last_server_set_digest, server_set_digest, 16)) {
        HT_INFOF("Statistics server set mismatch, using previous "
                 "statistics. last_server_count=%d, server_count=%d",
                 m_last_server_count, server_count);
        ts_iter->second.scan_rate = prev_iter->second.scan_rate;
        ts_iter->second.update_rate = prev_iter->second.update_rate;
        ts_iter->second.cell_read_rate = prev_iter->second.cell_read_rate;
        ts_iter->second.cell_write_rate = prev_iter->second.cell_write_rate;
        ts_iter->second.byte_read_rate = prev_iter->second.byte_read_rate;
        ts_iter->second.byte_write_rate = prev_iter->second.byte_write_rate;
        ts_iter->second.disk_read_rate = prev_iter->second.disk_read_rate;
        memcpy(m_last_server_set_digest, server_set_digest, 16);
        m_last_server_count = server_count;
      }
      else {
        double elapsed_time = (double)(ts_iter->second.fetch_timestamp - prev_iter->second.fetch_timestamp) / 1000000000.0;
        ts_iter->second.scan_rate = (ts_iter->second.scans - prev_iter->second.scans) / elapsed_time;
        ts_iter->second.update_rate = (ts_iter->second.updates - prev_iter->second.updates) / elapsed_time;
        ts_iter->second.cell_read_rate = (ts_iter->second.cells_read - prev_iter->second.cells_read) / elapsed_time;
        ts_iter->second.cell_write_rate = (ts_iter->second.cells_written - prev_iter->second.cells_written) / elapsed_time;
        ts_iter->second.byte_read_rate = (ts_iter->second.bytes_read - prev_iter->second.bytes_read) / elapsed_time;
        ts_iter->second.byte_write_rate = (ts_iter->second.bytes_written - prev_iter->second.bytes_written) / elapsed_time;
        ts_iter->second.disk_read_rate = (ts_iter->second.disk_bytes_read - prev_iter->second.disk_bytes_read) / elapsed_time;
      }
    }

    String table_file_name = ts_iter->first;
    String rrd_file = m_monitoring_table_dir + "/" + table_file_name + "_table_stats_v0.rrd";
    if (!FileUtils::exists(rrd_file)) {
        String dir;
        size_t slash_pos;
        slash_pos = table_file_name.rfind("/");
        if (slash_pos != string::npos) {
            dir = table_file_name.substr(0,slash_pos+1);
        }
        String table_dir = m_monitoring_table_dir + "/"+dir;
        if (!FileUtils::exists(table_dir)) {
            if (!FileUtils::mkdirs(table_dir)) {
                HT_THROW(Error::LOCAL_IO_ERROR, "Unable to create table dir");
            }
        }
        create_table_rrd(rrd_file);
    }
    update_table_rrd(rrd_file,ts_iter->second);
  }
  dump_table_summary_json();

  dump_master_summary_json();

  m_last_server_count = server_count;

}

void Monitoring::add_table_stats(std::vector<StatsTable> &table_stats,int64_t fetch_timestamp) {

  TableStatMap::iterator iter;

  for (size_t i=0; i<table_stats.size(); i++) {
    iter = m_table_stat_map.find(table_stats[i].table_id);
    struct table_rrd_data table_data;
    if (iter != m_table_stat_map.end()) {
      table_data = iter->second;
    } else {
      memset(&table_data, 0, sizeof(table_data));
    }
    table_data.fetch_timestamp = fetch_timestamp;
    table_data.range_count += table_stats[i].range_count;
    table_data.scanner_count += table_stats[i].scanner_count;
    table_data.cell_count += table_stats[i].cell_count;
    table_data.file_count += table_stats[i].file_count;
    table_data.scans += table_stats[i].scans;
    table_data.cells_read += table_stats[i].cells_scanned;
    table_data.bytes_read += table_stats[i].bytes_scanned;
    table_data.disk_bytes_read += table_stats[i].disk_bytes_read;
    table_data.updates += table_stats[i].updates;
    table_data.cells_written += table_stats[i].cells_written;
    table_data.bytes_written += table_stats[i].bytes_written;
    table_data.disk_used += table_stats[i].disk_used;
    table_data.average_key_size += table_stats[i].key_bytes;
    table_data.average_value_size += table_stats[i].value_bytes;
    table_data.compression_ratio += (double)table_stats[i].disk_used / table_stats[i].compression_ratio;
    table_data.memory_used += table_stats[i].memory_used;
    table_data.memory_allocated += table_stats[i].memory_allocated;
    table_data.shadow_cache_memory += table_stats[i].shadow_cache_memory;
    table_data.block_index_memory += table_stats[i].block_index_memory;
    table_data.bloom_filter_memory += table_stats[i].bloom_filter_memory;
    table_data.bloom_filter_accesses += table_stats[i].bloom_filter_accesses;
    table_data.bloom_filter_maybes += table_stats[i].bloom_filter_maybes;
    m_table_stat_map[table_stats[i].table_id] = table_data;
  }
}

void Monitoring::compute_clock_skew(int64_t server_timestamp, RangeServerStatistics *stats) {
  int64_t skew;
  int32_t multiplier = 1;

  if (server_timestamp < stats->fetch_timestamp) {
    skew = stats->fetch_timestamp - server_timestamp;
    multiplier = -1;
  }
  else
    skew = server_timestamp - stats->fetch_timestamp;

  // if the reading difference is less than it took to make the request
  // then leave the old skew value in place
  if (skew < stats->fetch_duration)
    return;

  // discount fetch duration and convert to microseconds
  skew -= stats->fetch_duration;
  skew /= 1000;

  if (skew < m_allowable_skew)
    stats->clock_skew = 0;
  else
    stats->clock_skew =  (skew / 1000000L) * multiplier;
}

void Monitoring::create_rangeserver_rrd(const String &filename) {
  char buf[64];
  String step;

  HT_ASSERT((m_monitoring_interval/1000)>0);

  sprintf(buf, "--step=%u", (unsigned)(m_monitoring_interval/1000));
  step = String(buf);

  /**
   * Create rrd file read rrdcreate man page to understand what this does
   * http://oss.oetiker.ch/rrdtool/doc/rrdcreate.en.html
   */

  HT_DEBUGF("Creating rrd file %s", filename.c_str());

  std::vector<String> args;
  args.push_back((String)"create");
  args.push_back(filename);
  args.push_back(step);
  args.push_back((String)"DS:range_count:GAUGE:600:0:U"); // num_ranges is not a rate, 600s heartbeat
  args.push_back((String)"DS:scanner_count:GAUGE:600:0:U");
  args.push_back((String)"DS:file_count:GAUGE:600:0:U");
  args.push_back((String)"DS:scan_rate:GAUGE:600:0:U"); // scans is a rate, 600s heartbeat
  args.push_back((String)"DS:update_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:sync_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:cell_read_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:cell_write_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:byte_read_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:byte_write_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:qcache_hit_pct:GAUGE:600:0:100");
  args.push_back((String)"DS:qcache_max_mem:GAUGE:600:0:U");
  args.push_back((String)"DS:qcache_fill:GAUGE:600:0:U");
  args.push_back((String)"DS:bcache_hit_pct:GAUGE:600:0:100");
  args.push_back((String)"DS:bcache_max_mem:GAUGE:600:0:U");
  args.push_back((String)"DS:bcache_fill:GAUGE:600:0:U");
  args.push_back((String)"DS:disk_used_pct:GAUGE:600:0:100");
  args.push_back((String)"DS:disk_read_bytes:GAUGE:600:0:U");
  args.push_back((String)"DS:disk_write_bytes:GAUGE:600:0:U");
  args.push_back((String)"DS:disk_read_iops:GAUGE:600:0:U");
  args.push_back((String)"DS:disk_write_iops:GAUGE:600:0:U");
  args.push_back((String)"DS:vm_size:GAUGE:600:0:U");
  args.push_back((String)"DS:vm_resident:GAUGE:600:0:U");
  args.push_back((String)"DS:page_in:GAUGE:600:0:U");
  args.push_back((String)"DS:page_out:GAUGE:600:0:U");
  args.push_back((String)"DS:heap_size:GAUGE:600:0:U");
  args.push_back((String)"DS:heap_slack:GAUGE:600:0:U");
  args.push_back((String)"DS:tracked_memory:GAUGE:600:0:U");
  args.push_back((String)"DS:net_rx_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:net_tx_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:loadavg:GAUGE:600:0:U");
  args.push_back((String)"DS:cpu_user:GAUGE:600:0:U");
  args.push_back((String)"DS:cpu_sys:GAUGE:600:0:U");

  args.push_back((String)"RRA:AVERAGE:.5:1:2880"); // higherst res (30s) has 2880 samples(1 day)
  args.push_back((String)"RRA:AVERAGE:.5:10:2880"); // 5min res for 10 days
  args.push_back((String)"RRA:AVERAGE:.5:60:1448"); // 30min res for 31 days
  args.push_back((String)"RRA:AVERAGE:.5:720:2190");// 6hr res for last 1.5 yrs
  args.push_back((String)"RRA:MAX:.5:10:2880"); // 5min res spikes for last 10 days
  args.push_back((String)"RRA:MAX:.5:720:2190");// 6hr res spikes for last 1.5 yrs

  run_rrdtool(args);
}

void Monitoring::create_table_rrd(const String &filename) {
  char buf[64];
  String step;

  HT_ASSERT((m_monitoring_interval/1000)>0);

  sprintf(buf, "--step=%u", (unsigned)(m_monitoring_interval/1000));
  step = String(buf);

  /**
   * Create rrd file read rrdcreate man page to understand what this does
   * http://oss.oetiker.ch/rrdtool/doc/rrdcreate.en.html
   */

  HT_DEBUGF("Creating rrd file %s", filename.c_str());

  std::vector<String> args;
  args.push_back((String)"create");
  args.push_back(filename);
  args.push_back(step);
  args.push_back((String)"DS:range_count:GAUGE:600:0:U"); // num_ranges is not a rate, 600s heartbeat
  args.push_back((String)"DS:scanner_count:GAUGE:600:0:U");
  args.push_back((String)"DS:scan_rate:GAUGE:600:0:U"); // scans is a rate, 600s heartbeat
  args.push_back((String)"DS:update_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:cell_read_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:cell_write_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:byte_read_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:byte_write_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:disk_read_rate:GAUGE:600:0:U");
  args.push_back((String)"DS:disk_used:GAUGE:600:0:U");
  args.push_back((String)"DS:compression_ratio:GAUGE:600:0:U");
  args.push_back((String)"DS:memory_used:GAUGE:600:0:U");
  args.push_back((String)"DS:memory_allocated:GAUGE:600:0:U");
  args.push_back((String)"DS:shadow_cache_memory:GAUGE:600:0:U");
  args.push_back((String)"DS:block_index_memory:GAUGE:600:0:U");
  args.push_back((String)"DS:bloom_filter_memory:GAUGE:600:0:U");
  args.push_back((String)"DS:bloom_filter_access:GAUGE:600:0:U");
  args.push_back((String)"DS:bloom_filter_maybes:GAUGE:600:0:U");

  args.push_back((String)"RRA:AVERAGE:.5:1:2880"); // higherst res (30s) has 2880 samples(1 day)
  args.push_back((String)"RRA:AVERAGE:.5:10:2880"); // 5min res for 10 days
  args.push_back((String)"RRA:AVERAGE:.5:60:1448"); // 30min res for 31 days
  args.push_back((String)"RRA:AVERAGE:.5:720:2190");// 6hr res for last 1.5 yrs
  args.push_back((String)"RRA:MAX:.5:10:2880"); // 5min res spikes for last 10 days
  args.push_back((String)"RRA:MAX:.5:720:2190");// 6hr res spikes for last 1.5 yrs

  run_rrdtool(args);
}

void Monitoring::update_table_rrd(const String &filename, struct table_rrd_data &rrd_data) {
  std::vector<String> args;
  String update;

  args.push_back((String)"update");
  args.push_back(filename);

  update = format("%llu:%d:%d:%.2f:%.2f:%.2f:%.2f:%.2f:%.2f:%.2f:%lld:%.2f:%lld:%lld:%lld:%lld:%lld:%lld:%lld",
		  (Llu)table_stats_timestamp,
		  rrd_data.range_count,
		  rrd_data.scanner_count,
		  rrd_data.scan_rate,
		  rrd_data.update_rate,
		  rrd_data.cell_read_rate,
		  rrd_data.cell_write_rate,
		  rrd_data.byte_read_rate,
		  rrd_data.byte_write_rate,
		  rrd_data.disk_read_rate,
		  (Lld)rrd_data.disk_used,
		  rrd_data.compression_ratio,
		  (Lld)rrd_data.memory_used,
		  (Lld)rrd_data.memory_allocated,
		  (Lld)rrd_data.shadow_cache_memory,
		  (Lld)rrd_data.block_index_memory,
		  (Lld)rrd_data.bloom_filter_memory,
		  (Lld)rrd_data.bloom_filter_accesses,
		  (Lld)rrd_data.bloom_filter_maybes);

  HT_DEBUGF("table update=\"%s\"", update.c_str());

  args.push_back(update);

  run_rrdtool(args);
}

void Monitoring::update_rangeserver_rrd(const String &filename, struct rangeserver_rrd_data &rrd_data) {
  std::vector<String> args;
  String update;

  args.push_back((String)"update");
  args.push_back(filename);

  update = format("%llu:%d:%d:%lld:%.2f:%.2f:%.2f:%.2f:%.2f:%.2f:%.2f:%.2f:%lld:%lld:%.2f:%lld:%lld:%.2f:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%.2f:%.2f:%.2f:%.2f:%.2f",
                  (Llu)rrd_data.timestamp,
                  rrd_data.range_count,
                  rrd_data.scanner_count,
                  (Llu)rrd_data.file_count,
                  rrd_data.scan_rate,
                  rrd_data.update_rate,
                  rrd_data.sync_rate,
                  rrd_data.cell_read_rate,
                  rrd_data.cell_write_rate,
                  rrd_data.byte_read_rate,
                  rrd_data.byte_write_rate,
                  rrd_data.qcache_hit_pct,
                  (Lld)rrd_data.qcache_max_mem,
                  (Lld)rrd_data.qcache_fill,
                  rrd_data.bcache_hit_pct,
                  (Lld)rrd_data.bcache_max_mem,
                  (Lld)rrd_data.bcache_fill,
                  rrd_data.disk_used_pct,
                  (Lld)rrd_data.disk_read_bytes,
                  (Lld)rrd_data.disk_write_bytes,
                  (Lld)rrd_data.disk_read_iops,
                  (Lld)rrd_data.disk_write_iops,
                  (Lld)rrd_data.vm_size,
                  (Lld)rrd_data.vm_resident,
                  (Lld)rrd_data.page_in,
                  (Lld)rrd_data.page_out,
                  (Lld)rrd_data.heap_size,
                  (Lld)rrd_data.heap_slack,
                  (Lld)rrd_data.tracked_memory,
                  rrd_data.net_rx_rate,
                  rrd_data.net_tx_rate,
                  rrd_data.load_average,
                  rrd_data.cpu_user,
                  rrd_data.cpu_sys);

  HT_DEBUGF("update=\"%s\"", update.c_str());

  args.push_back(update);

  run_rrdtool(args);
}

namespace {
  const char *rs_json_header = "{\"RangeServerSummary\": {\n  \"servers\": [\n";
  const char *rs_json_footer= "\n  ]\n}}\n";
  const char *rs_entry_format =
    "{\"order\": \"%d\", \"location\": \"%s\", \"version\": \"%s\", \"hostname\": \"%s\", \"ip\": \"%s\", \"arch\": \"%s\","
    " \"cores\": \"%d\", \"skew\": \"%d\", \"os\": \"%s\", \"osVersion\": \"%s\","
    " \"vendor\": \"%s\", \"vendorVersion\": \"%s\", \"ram\": \"%.2f\","
    " \"disk\": \"%.2f\", \"diskUsePct\": \"%u\", \"rangeCount\": \"%llu\","
    " \"lastContact\": \"%s\", \"lastError\": \"%s\"}";

  const char *master_json_header = "{\"MasterSummary\": {\"version\": \"%s\", \"name\": \"%s\", \"state\": [\n";
  const char *master_json_footer = "\n]}}\n";
  const char *state_variable_format = "{\"name\": \"%s\", \"value\": \"%s\"}";

  const char *table_json_header = "{\"TableSummary\": {\n  \"tables\": [\n";
  const char *table_json_footer= "\n  ]\n}}\n";
  const char *table_entry_format =
    "{\"id\": \"%s\",\"name\": \"%s\",\"rangecount\": \"%u\", \"cellcount\": \"%llu\", \"filecount\": \"%llu\", \"disk\": \"%llu\","
    " \"memory\": \"%llu\", \"average_key_size\": \"%.1f\", \"average_value_size\": \"%.1f\", \"compression_ratio\": \"%.3f\"}";
}

void Monitoring::dump_master_summary_json() {
  String contents = format(master_json_header, version_string(),
                           m_context->cluster_name.c_str());
  String entry;
  String tmp_filename = m_monitoring_dir + "/master_summary.tmp";
  String json_filename = m_monitoring_dir + "/master_summary.json";
  std::vector<SystemVariable::Spec> specs;

  m_context->system_state->get_non_default(specs);
  for (size_t i = 0; i<specs.size(); i++) {
    entry = format(state_variable_format,
                   SystemVariable::code_to_string(specs[i].code),
                   specs[i].value ? "true" : "false");
    if (i == 0)
      contents += String("    ") + entry;
    else
      contents += String(",\n    ") + entry;
  }
  contents += master_json_footer;

  if (FileUtils::write(tmp_filename, contents) == -1)
    return;
  FileUtils::rename(tmp_filename, json_filename);
}

void Monitoring::dump_rangeserver_summary_json(std::vector<RangeServerStatistics> &stats) {
  String str = String(rs_json_header);
  String entry;
  double ram;
  double disk;
  unsigned disk_use_pct;
  String error_str;
  String contact_time;
  uint64_t range_count;
  const char *version_string = "";

  for (size_t i=0; i<stats.size(); i++) {
    if (stats[i].stats) {
      double numerator=0.0, denominator=0.0;
      ram = stats[i].stats->system.mem_stat.ram / 1000.0;
      disk = 0.0;
      disk_use_pct = 0;
      for (size_t j=0; j<stats[i].stats->system.fs_stat.size(); j++) {
        numerator += stats[i].stats->system.fs_stat[j].total -
          stats[i].stats->system.fs_stat[j].avail;
        denominator += stats[i].stats->system.fs_stat[j].total;
        disk += stats[i].stats->system.fs_stat[j].total;
      }
      disk /= 1000000000.0;
      disk_use_pct = (unsigned)((numerator/denominator)*100.0);
      time_t contact = (time_t)(stats[i].fetch_timestamp / 1000000000LL);
      char buf[64];
      ctime_r(&contact, buf);
      contact_time = buf;
      boost::trim(contact_time);
      range_count = stats[i].stats->range_count;
      version_string = stats[i].stats->version.c_str();
    }
    else {
      ram = 0.0;
      disk = 0.0;
      disk_use_pct = 0;
      contact_time = String("N/A");
      range_count = 0;
    }

    if (stats[i].fetch_error == 0)
      error_str = "ok";
    else {
      error_str = stats[i].fetch_error_msg;
      if (error_str.empty())
        error_str = Error::get_text(stats[i].fetch_error);
    }

    entry = format(rs_entry_format,
                   i,
                   stats[i].location.c_str(),
                   version_string,
                   stats[i].system_info->net_info.host_name.c_str(),
                   stats[i].system_info->net_info.primary_addr.c_str(),
                   stats[i].system_info->os_info.arch.c_str(),
                   stats[i].system_info->cpu_info.total_cores,
                   stats[i].clock_skew,
                   stats[i].system_info->os_info.name.c_str(),
                   stats[i].system_info->os_info.version.c_str(),
                   stats[i].system_info->os_info.vendor.c_str(),
                   stats[i].system_info->os_info.vendor_version.c_str(),
                   ram,
                   disk,
                   disk_use_pct,
                   (Llu)range_count,
                   contact_time.c_str(),
                   error_str.c_str());

    if (i != 0)
      str += String(",\n    ") + entry;
    else
      str += String("    ") + entry;
  }

  str += rs_json_footer;

  String tmp_filename = m_monitoring_dir + "/rangeserver_summary.tmp";
  String json_filename = m_monitoring_dir + "/rangeserver_summary.json";

  if (FileUtils::write(tmp_filename, str) == -1)
    return;

  FileUtils::rename(tmp_filename, json_filename);

}

void Monitoring::dump_table_summary_json() {
  String str = String(table_json_header);
  String entry;
  struct table_rrd_data table_data;
  String table_id;
  String table_name;
  TableStatMap::iterator ts_iter;
  int i = 0;
  for (ts_iter = m_table_stat_map.begin();
      ts_iter != m_table_stat_map.end(); ++ts_iter) {
    table_id = ts_iter->first;
    table_data = ts_iter->second;
    TableNameMap::iterator tn_iter = m_table_name_map.find(table_id);
    if (tn_iter != m_table_name_map.end()) {
      table_name = tn_iter->second;
    }
    else {
      m_namemap_ptr->id_to_name(table_id,table_name);
      m_table_name_map[table_id] = table_name;
    }
    entry = format(table_entry_format,
                   table_id.c_str(),
                   table_name.c_str(),
                   (unsigned)table_data.range_count,
                   (Llu)table_data.cell_count,
                   (Llu)table_data.file_count,
                   (Llu)table_data.disk_used,
                   (Llu)table_data.memory_used,
                   table_data.average_key_size,
                   table_data.average_value_size,
                   table_data.compression_ratio);
    if (i != 0)
      str += String(",\n    ") + entry;
    else
      str += String("    ") + entry;
    i++;
  }

  str += table_json_footer;

  String tmp_filename = m_monitoring_dir + "/table_summary.tmp";
  String json_filename = m_monitoring_dir + "/table_summary.json";

  if (FileUtils::write(tmp_filename, str) == -1)
    return;

  FileUtils::rename(tmp_filename, json_filename);
}

void Monitoring::change_id_mapping(const String &table_id, const String &table_name) {
  String s_table_id(table_id);
  String s_table_name(table_name);
  boost::trim_if(s_table_name, boost::is_any_of("/"));
  m_table_name_map[s_table_id] = s_table_name;
}

void Monitoring::invalidate_id_mapping(const String &table_id) {
  m_table_name_map.erase(table_id);
}

void Monitoring::run_rrdtool(std::vector<String> &command) {
  if (m_disable_rrdtool)
    return;

  String cmd = "env LD_LIBRARY_PATH= DYLD_LIBRARY_PATH= rrdtool";

  for (auto &s : command) {
    cmd += " \"";
    cmd += s;
    cmd += "\" ";
  }

  HT_DEBUGF("run_rrdtool: %s", cmd.c_str());

  int ret = ::system(cmd.c_str());
  if (ret != 0) {
    HT_WARNF("Monitor: failed to invoke `rrdtool`; make sure it's properly "
            "installed and in your $PATH (command %s returned status %d)", cmd.c_str(), ret);
  }
}

