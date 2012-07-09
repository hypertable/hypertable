/**
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
#include "Common/Config.h"
#include "Common/md5.h"
#include "Common/SystemInfo.h"

#include <algorithm>
#include <limits>
#include <iostream>
#include <fstream>

#include "Global.h"
#include "MaintenanceFlag.h"
#include "MaintenancePrioritizerLogCleanup.h"
#include "MaintenanceScheduler.h"
#include "MaintenanceTaskCompaction.h"
#include "MaintenanceTaskMemoryPurge.h"
#include "MaintenanceTaskRelinquish.h"
#include "MaintenanceTaskSplit.h"
#include "MaintenanceTaskWorkQueue.h"

using namespace Hypertable;
using namespace std;
using namespace Hypertable::Config;

namespace {
  struct RangeStatsAscending {
    bool operator()(const Range::MaintenanceData *x, const Range::MaintenanceData *y) const {
      return x->priority < y->priority;
    }
  };
}


MaintenanceScheduler::MaintenanceScheduler(MaintenanceQueuePtr &queue, RSStatsPtr &server_stats,
                                           RangeStatsGathererPtr &gatherer)
  : m_initialized(false), m_scheduling_needed(false), m_queue(queue),
    m_server_stats(server_stats), m_stats_gatherer(gatherer),
    m_prioritizer_log_cleanup(server_stats),
    m_prioritizer_low_memory(server_stats) {
  m_prioritizer = &m_prioritizer_log_cleanup;
  m_maintenance_interval = get_i32("Hypertable.RangeServer.Maintenance.Interval");
  m_query_cache_memory = get_i64("Hypertable.RangeServer.QueryCache.MaxMemory");
  // Setup to immediately schedule maintenance
  boost::xtime_get(&m_last_maintenance, TIME_UTC);
  memcpy(&m_last_low_memory, &m_last_maintenance, sizeof(boost::xtime));
  memcpy(&m_last_check, &m_last_maintenance, sizeof(boost::xtime));
  m_last_maintenance.sec -= m_maintenance_interval / 1000;
  m_low_memory_limit_percentage = get_i32("Hypertable.RangeServer.LowMemoryLimit.Percentage");
  m_merging_delay = get_i32("Hypertable.RangeServer.Maintenance.MergingCompaction.Delay");
  m_merges_per_interval = get_i32("Hypertable.RangeServer.Maintenance.MergesPerInterval",
                                  std::numeric_limits<int32_t>::max());
  m_move_compactions_per_interval = get_i32("Hypertable.RangeServer.Maintenance.MoveCompactionsPerInterval");

  /** 
   * This code adds hashes for the four primary commit log
   * directories to the m_log_hashes set.  This set is passed
   * into CommitLog::purge to tell the commit log that fragments
   * from these directories are OK to remove.
   */
  String log_dir = Global::log_dir + "/root";
  m_log_hashes.insert( md5_hash(log_dir.c_str()) );
  log_dir = Global::log_dir + "/metadata";
  m_log_hashes.insert( md5_hash(log_dir.c_str()) );
  log_dir = Global::log_dir + "/system";
  m_log_hashes.insert( md5_hash(log_dir.c_str()) );
  log_dir = Global::log_dir + "/user";
  m_log_hashes.insert( md5_hash(log_dir.c_str()) );

}



void MaintenanceScheduler::schedule() {
  RangeStatsVector range_data;
  RangeStatsVector range_data_prioritized;
  AccessGroup::MaintenanceData *ag_data;
  String output;
  String ag_name;
  String trace_str;
  int64_t excess = 0;
  MaintenancePrioritizer::MemoryState memory_state;
  bool low_memory = low_memory_mode();
  int32_t priority = 1;

  memory_state.balance = Global::memory_tracker->balance();
  memory_state.limit = Global::memory_limit;

  // adjust limit if it makes sense
  if (Global::memory_limit_ensure_unused_current &&
      memory_state.balance - m_query_cache_memory > Global::memory_limit_ensure_unused_current) {
    excess = Global::memory_limit_ensure_unused_current
                       - (int64_t)(System::mem_stat().free * Property::MiB);
    if (excess > 0)
      memory_state.limit = memory_state.balance - excess;
  }

  if (low_memory) {
    if (Global::maintenance_queue->pending() < Global::maintenance_queue->workers())
      m_scheduling_needed = true;
    excess = (memory_state.balance > memory_state.limit) ? memory_state.balance - memory_state.limit : 0;
    memory_state.needed = ((memory_state.limit * m_low_memory_limit_percentage) / 100) + excess;
  }

  trace_str += String("low_memory\t") + (low_memory ? "true" : "false") + "\n";
  trace_str += String("Global::memory_tracker->balance()\t") + Global::memory_tracker->balance() + "\n";
  trace_str += String("Global::memory_limit\t") + Global::memory_limit + "\n";
  trace_str += String("Global::memory_limit_ensure_unused_current\t") + Global::memory_limit_ensure_unused_current + "\n";
  trace_str += String("m_query_cache_memory\t") + m_query_cache_memory + "\n";
  trace_str += String("excess\t") + excess + "\n";
  trace_str += String("Global::maintenance_queue->pending()\t") + (int)Global::maintenance_queue->pending() + "\n";
  trace_str += String("Global::maintenance_queue->workers()\t") + (int)Global::maintenance_queue->workers() + "\n";
  trace_str += String("m_low_memory_limit_percentage\t") + m_low_memory_limit_percentage + "\n";
  trace_str += String("memory_state.balance\t") + memory_state.balance + "\n";
  trace_str += String("memory_state.limit\t") + memory_state.limit + "\n";
  trace_str += String("memory_state.needed\t") + memory_state.needed + "\n";
  {
    uint64_t max_memory = 0;
    uint64_t available_memory = 0;
    uint64_t accesses = 0;
    uint64_t hits = 0;
    if (Global::block_cache)
      Global::block_cache->get_stats(&max_memory, &available_memory, &accesses, &hits);
    trace_str += String("FileBlockCache-max_memory\t") + max_memory + "\n";
    trace_str += String("FileBlockCache-available_memory\t") + available_memory + "\n";
    trace_str += String("FileBlockCache-accesses\t") + accesses + "\n";
    trace_str += String("FileBlockCache-hits\t") + hits + "\n";
  }

  boost::xtime now;
  boost::xtime_get(&now, TIME_UTC);
  int64_t millis_since_last_maintenance =
    xtime_diff_millis(m_last_maintenance, now);

  int collector_id = RSStats::STATS_COLLECTOR_MAINTENANCE;
  bool do_merges = !low_memory && 
    ((m_server_stats->get_update_bytes(collector_id) < 1000 && m_server_stats->get_scan_count(collector_id) < 5) ||
     xtime_diff_millis(m_last_low_memory, now) >= (int64_t)m_merging_delay);

  if (!m_scheduling_needed &&
      millis_since_last_maintenance < m_maintenance_interval)
    return;

  Global::maintenance_queue->clear();

  m_stats_gatherer->fetch(range_data);

  if (range_data.empty()) {
    m_scheduling_needed = false;
    return;
  }

  HT_ASSERT(m_prioritizer);

  int64_t block_index_memory = 0;
  int64_t bloom_filter_memory = 0;
  int64_t cell_cache_memory = 0;
  int64_t shadow_cache_memory = 0;
  int64_t not_acknowledged = 0;

  /**
   * Purge commit log fragments
   */
  {
    int64_t revision_user = TIMESTAMP_MAX;
    int64_t revision_metadata = TIMESTAMP_MAX;
    int64_t revision_system = TIMESTAMP_MAX;
    int64_t revision_root = TIMESTAMP_MAX;
    AccessGroup::CellStoreMaintenanceData *cs_data;
    std::set<int64_t> log_dir_hashes = m_log_hashes;

    trace_str += String("before revision_root\t") + revision_root + "\n";
    trace_str += String("before revision_metadata\t") + revision_metadata + "\n";
    trace_str += String("before revision_system\t") + revision_system + "\n";
    trace_str += String("before revision_user\t") + revision_user + "\n";

    for (size_t i=0; i<range_data.size(); i++) {

      log_dir_hashes.insert(range_data[i]->log_hash);

      if (range_data[i]->needs_major_compaction && priority <= m_move_compactions_per_interval) {
        range_data[i]->priority = priority++;
        range_data[i]->maintenance_flags = MaintenanceFlag::COMPACT_MOVE;
        continue;
      }

      if (!range_data[i]->load_acknowledged)
        not_acknowledged++;

      for (ag_data = range_data[i]->agdata; ag_data; ag_data = ag_data->next) {

        // compute memory stats
        cell_cache_memory += ag_data->mem_allocated;
        for (cs_data = ag_data->csdata; cs_data; cs_data = cs_data->next) {
          shadow_cache_memory += cs_data->shadow_cache_size;
          block_index_memory += cs_data->index_stats.block_index_memory;
          bloom_filter_memory += cs_data->index_stats.bloom_filter_memory;
        }

        if (ag_data->earliest_cached_revision != TIMESTAMP_MAX) {
          if (range_data[i]->range->is_root()) {
            if (ag_data->earliest_cached_revision < revision_root)
              revision_root = ag_data->earliest_cached_revision;
          }
          else if (range_data[i]->is_metadata) {
            if (ag_data->earliest_cached_revision < revision_metadata)
              revision_metadata = ag_data->earliest_cached_revision;
          }
          else if (range_data[i]->is_system) {
            if (ag_data->earliest_cached_revision < revision_system)
              revision_system = ag_data->earliest_cached_revision;
          }
          else {
            if (ag_data->earliest_cached_revision < revision_user)
              revision_user = ag_data->earliest_cached_revision;
          }
        }
      }
    }

    trace_str += String("after revision_root\t") + revision_root + "\n";
    trace_str += String("after revision_metadata\t") + revision_metadata + "\n";
    trace_str += String("after revision_system\t") + revision_system + "\n";
    trace_str += String("after revision_user\t") + revision_user + "\n";

    if (Global::root_log)
      Global::root_log->purge(revision_root, log_dir_hashes);

    if (Global::metadata_log)
      Global::metadata_log->purge(revision_metadata, log_dir_hashes);

    if (Global::system_log)
      Global::system_log->purge(revision_system, log_dir_hashes);

    if (Global::user_log)
      Global::user_log->purge(revision_user, log_dir_hashes);
  }

  {
    int64_t block_cache_memory = Global::block_cache ? Global::block_cache->memory_used() : 0;
    int64_t total_memory = block_cache_memory + block_index_memory + bloom_filter_memory + cell_cache_memory + shadow_cache_memory + m_query_cache_memory;
    double block_cache_pct = ((double)block_cache_memory / (double)total_memory) * 100.0;
    double block_index_pct = ((double)block_index_memory / (double)total_memory) * 100.0;
    double bloom_filter_pct = ((double)bloom_filter_memory / (double)total_memory) * 100.0;
    double cell_cache_pct = ((double)cell_cache_memory / (double)total_memory) * 100.0;
    double shadow_cache_pct = ((double)shadow_cache_memory / (double)total_memory) * 100.0;
    double query_cache_pct = ((double)m_query_cache_memory / (double)total_memory) * 100.0;

    HT_INFOF("Memory Statistics (MB): VM=%.2f, RSS=%.2f, tracked=%.2f, computed=%.2f limit=%.2f",
             System::proc_stat().vm_size, System::proc_stat().vm_resident,
             (double)memory_state.balance/(double)Property::MiB, (double)total_memory/(double)Property::MiB,
             (double)Global::memory_limit/(double)Property::MiB);
    HT_INFOF("Memory Allocation: BlockCache=%.2f%% BlockIndex=%.2f%% "
             "BloomFilter=%.2f%% CellCache=%.2f%% ShadowCache=%.2f%% "
             "QueryCache=%.2f%%",
             block_cache_pct, block_index_pct, bloom_filter_pct,
             cell_cache_pct, shadow_cache_pct, query_cache_pct);
  }

  String dummy_str;
  m_prioritizer->prioritize(range_data, memory_state, priority, dummy_str);

  check_file_dump_statistics(now, range_data, trace_str);

  boost::xtime schedule_time;
  boost::xtime_get(&schedule_time, boost::TIME_UTC);

  if (not_acknowledged) {
    HT_INFO_OUT << "Found load_acknowledged=false in " << not_acknowledged
        << " ranges" << HT_END;
  }

  // if this is the first time around, just enqueue work that
  // was in progress
  if (!m_initialized) {
    int level = 0, priority = 0;
    for (size_t i=0; i<range_data.size(); i++) {
      if (range_data[i]->state == RangeState::SPLIT_LOG_INSTALLED ||
          range_data[i]->state == RangeState::SPLIT_SHRUNK) {
        RangePtr range(range_data[i]->range);
        level = get_level(range);
        Global::maintenance_queue->add(new MaintenanceTaskSplit(level, priority++, schedule_time, range));
      }
      else if (range_data[i]->state == RangeState::RELINQUISH_LOG_INSTALLED) {
        RangePtr range(range_data[i]->range);
        level = get_level(range);
        Global::maintenance_queue->add(new MaintenanceTaskRelinquish(level, priority++, schedule_time, range));
      }
    }
    m_initialized = true;
  }
  else {

    // Sort the ranges based on priority
    range_data_prioritized.reserve( range_data.size() );
    for (size_t i=0; i<range_data.size(); i++) {
      if (range_data[i]->priority > 0)
        range_data_prioritized.push_back(range_data[i]);
    }
    struct RangeStatsAscending ordering;
    sort(range_data_prioritized.begin(), range_data_prioritized.end(), ordering);

    int32_t merges_created = 0;
    int level = 0;

    for (size_t i=0; i<range_data_prioritized.size(); i++) {
      if (range_data_prioritized[i]->maintenance_flags & MaintenanceFlag::SPLIT) {
        RangePtr range(range_data_prioritized[i]->range);
        level = get_level(range);
        Global::maintenance_queue->add(new MaintenanceTaskSplit(level, range_data_prioritized[i]->priority, schedule_time, range));
      }
      else if (range_data_prioritized[i]->maintenance_flags & MaintenanceFlag::RELINQUISH) {
        RangePtr range(range_data_prioritized[i]->range);
        level = get_level(range);
        Global::maintenance_queue->add(new MaintenanceTaskRelinquish(level, range_data_prioritized[i]->priority, schedule_time, range));
      }
      else if (range_data_prioritized[i]->maintenance_flags & MaintenanceFlag::COMPACT) {
        MaintenanceTaskCompaction *task;
        RangePtr range(range_data_prioritized[i]->range);
        level = get_level(range);
        task = new MaintenanceTaskCompaction(level, range_data_prioritized[i]->priority, schedule_time, range);
        if (!range_data_prioritized[i]->needs_major_compaction) {
          for (AccessGroup::MaintenanceData *ag_data=range_data_prioritized[i]->agdata; ag_data; ag_data=ag_data->next) {
            if (MaintenanceFlag::minor_compaction(ag_data->maintenance_flags) ||
                MaintenanceFlag::major_compaction(ag_data->maintenance_flags) ||
                MaintenanceFlag::gc_compaction(ag_data->maintenance_flags))
              task->add_subtask(ag_data->ag, ag_data->maintenance_flags);
            else if (MaintenanceFlag::merging_compaction(ag_data->maintenance_flags)) {
              if (do_merges && merges_created < m_merges_per_interval) {
                task->add_subtask(ag_data->ag, ag_data->maintenance_flags);
                merges_created++;
              }
            }
          }
        }
        Global::maintenance_queue->add(task);
      }
      else if (range_data_prioritized[i]->maintenance_flags & MaintenanceFlag::MEMORY_PURGE) {
        MaintenanceTaskMemoryPurge *task;
        RangePtr range(range_data_prioritized[i]->range);
        level = get_level(range);
        task = new MaintenanceTaskMemoryPurge(level, range_data_prioritized[i]->priority, schedule_time, range);
        for (AccessGroup::MaintenanceData *ag_data=range_data_prioritized[i]->agdata; ag_data; ag_data=ag_data->next) {
          if (ag_data->maintenance_flags & MaintenanceFlag::MEMORY_PURGE) {
            task->add_subtask(ag_data->ag, ag_data->maintenance_flags);
            for (AccessGroup::CellStoreMaintenanceData *cs_data=ag_data->csdata; cs_data; cs_data=cs_data->next) {
              if (cs_data->maintenance_flags & MaintenanceFlag::MEMORY_PURGE)
                task->add_subtask(cs_data->cs, cs_data->maintenance_flags);
            }
          }
        }
        Global::maintenance_queue->add(task);
      }
    }
  }

  MaintenanceTaskWorkQueue *task = 0;
  {
    ScopedLock lock(Global::mutex);
    if (!Global::work_queue.empty())
      task = new MaintenanceTaskWorkQueue(3, 0, Global::work_queue);
  }
  if (task)
    Global::maintenance_queue->add(task);

  //cout << flush << trace_str << flush;

  m_scheduling_needed = false;

  m_stats_gatherer->clear();
}

int MaintenanceScheduler::get_level(RangePtr &range) {
  if (range->is_root())
    return 0;
  if (range->metalog_entity()->table.is_metadata())
    return 1;
  else if (range->metalog_entity()->table.is_system())
    return 2;
  return 3;
}


void MaintenanceScheduler::check_file_dump_statistics(boost::xtime now, RangeStatsVector &range_data,
                                                      const String &header_str) {
  AccessGroup::MaintenanceData *ag_data;

  if (xtime_diff_millis(m_last_check, now) >= (int64_t)60000) {
    if (FileUtils::exists(System::install_dir + "/run/debug-scheduler")) {
      String output_fname = System::install_dir + "/run/scheduler.output";
      ofstream out;
      out.open(output_fname.c_str());
      out << header_str << "\n";
      for (size_t i=0; i<range_data.size(); i++) {
        out << *range_data[i] << "\n";
        for (ag_data = range_data[i]->agdata; ag_data; ag_data = ag_data->next)
          out << *ag_data << "\n";
      }
      out.close();
      FileUtils::unlink(System::install_dir + "/run/debug-scheduler");
    }
    m_last_check = now;
  }
  
}
