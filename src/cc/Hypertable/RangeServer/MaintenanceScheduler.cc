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

/// @file
/// Definitions for MaintenanceScheduler.
/// This file contains type definitions for MaintenanceScheduler, a class for
/// scheduling range server maintenance (e.g. compactions, splits, memory
/// purging, ...).

#include <Common/Compat.h>
#include "MaintenanceScheduler.h"

#include <Hypertable/RangeServer/Global.h>
#include <Hypertable/RangeServer/MaintenanceFlag.h>
#include <Hypertable/RangeServer/MaintenancePrioritizerLogCleanup.h>
#include <Hypertable/RangeServer/MaintenanceTaskCompaction.h>
#include <Hypertable/RangeServer/MaintenanceTaskDeferredInitialization.h>
#include <Hypertable/RangeServer/MaintenanceTaskMemoryPurge.h>
#include <Hypertable/RangeServer/MaintenanceTaskRelinquish.h>
#include <Hypertable/RangeServer/MaintenanceTaskSplit.h>
#include <Hypertable/RangeServer/MaintenanceTaskWorkQueue.h>

#include <Common/Config.h>
#include <Common/SystemInfo.h>
#include <Common/TimeWindow.h>
#include <Common/md5.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>

using namespace Hypertable;
using namespace std;
using namespace Hypertable::Config;

namespace {
  struct RangeDataAscending {
    bool operator()(const RangeData x, const RangeData y) const {
      return x.data->priority < y.data->priority;
    }
  };
}


MaintenanceScheduler::MaintenanceScheduler(MaintenanceQueuePtr &queue,
                                           TableInfoMapPtr &live_map)
  : m_queue(queue), m_live_map(live_map), m_start_offset(0),
    m_initialized(false), m_low_memory_mode(false) {
  m_prioritizer = &m_prioritizer_log_cleanup;
  m_maintenance_interval = get_i32("Hypertable.RangeServer.Maintenance.Interval");
  m_query_cache_memory = get_i64("Hypertable.RangeServer.QueryCache.MaxMemory");
  m_low_memory_prioritization = get_bool("Hypertable.RangeServer.Maintenance.LowMemoryPrioritization");

  // Setup to immediately schedule maintenance
  m_last_low_memory = chrono::steady_clock::now();
  m_last_check = m_last_low_memory;

  m_low_memory_limit_percentage = get_i32("Hypertable.RangeServer.LowMemoryLimit.Percentage");
  m_merging_delay = get_i32("Hypertable.RangeServer.Maintenance.MergingCompaction.Delay");
  m_merges_per_interval = get_i32("Hypertable.RangeServer.Maintenance.MergesPerInterval",
                                  std::numeric_limits<int32_t>::max());
  m_move_compactions_per_interval = get_i32("Hypertable.RangeServer.Maintenance.MoveCompactionsPerInterval");
  m_initialization_per_interval = get_i32("Hypertable.RangeServer.Maintenance.InitializationPerInterval",
                                          std::numeric_limits<int32_t>::max());

  m_maintenance_queue_worker_count = 
    (int32_t)Global::maintenance_queue->worker_count();
}


void MaintenanceScheduler::exclude(const TableIdentifier &table) {
  lock_guard<mutex> lock(m_mutex);
  if (m_table_blacklist.count(table.id) > 0)
    return;
  m_table_blacklist.insert(table.id);
  // Drop range maintenance tasks for table ID
  function<bool(Range *)> drop_predicate =
    [table](Range *r) -> bool {return r->get_table_id().compare(table.id)==0;};
  Global::maintenance_queue->drop_range_tasks(drop_predicate);
}

void MaintenanceScheduler::include(const TableIdentifier &table) {
  lock_guard<mutex> lock(m_mutex);
  m_table_blacklist.erase(table.id);
}



void MaintenanceScheduler::schedule() {
  Ranges ranges;
  Ranges ranges_prioritized;
  AccessGroup::MaintenanceData *ag_data;
  String output;
  String ag_name;
  String trace_str;
  int64_t excess = 0;
  MaintenancePrioritizer::MemoryState memory_state;
  int32_t priority = 1;
  bool low_memory = low_memory_mode();
  bool do_scheduling = true;
  bool debug = false;
  function<bool(RangeData &)> in_blacklist =
    [this](RangeData &rd) -> bool {return this->m_table_blacklist.count(rd.data->table_id);};

  auto now = chrono::steady_clock::now();

  Global::load_statistics->recompute();

  debug = debug_signal_file_exists(now);

  memory_state.balance = Global::memory_tracker->balance();
  memory_state.limit = Global::memory_limit;

  // adjust limit if it makes sense
  if (Global::memory_limit_ensure_unused_current &&
      memory_state.balance - m_query_cache_memory > Global::memory_limit_ensure_unused_current) {
    excess = Global::memory_limit_ensure_unused_current
                       - (int64_t)(System::mem_stat().free * MiB);
    if (excess > 0)
      memory_state.limit = memory_state.balance - excess;
  }

  if (low_memory) {
    if (Global::maintenance_queue->full())
      do_scheduling = false;
    excess = (memory_state.balance > memory_state.limit) ? memory_state.balance - memory_state.limit : 0;
    memory_state.needed = ((memory_state.limit * m_low_memory_limit_percentage) / 100) + excess;
  }

  if (debug) {
    trace_str += String("low_memory\t") + (low_memory ? "true" : "false") + "\n";
    trace_str += format("Global::memory_tracker->balance()\t%lld\n", (Lld)Global::memory_tracker->balance());
    trace_str += format("Global::memory_limit\t%lld\n", (Lld)Global::memory_limit);
    trace_str += format("Global::memory_limit_ensure_unused_current\t%lld\n", (Lld)Global::memory_limit_ensure_unused_current);
    trace_str += format("m_query_cache_memory\t%lld\n", (Lld)m_query_cache_memory);
    trace_str += format("excess\t%lld\n", (Lld)excess);
    trace_str += String("Global::maintenance_queue->full()\t") + (Global::maintenance_queue->full() ? "true" : "false") + "\n";
    trace_str += format("m_low_memory_limit_percentage\t%lld\n", (Lld)m_low_memory_limit_percentage);
    trace_str += format("memory_state.balance\t%lld\n", (Lld)memory_state.balance);
    trace_str += format("memory_state.limit\t%lld\n", (Lld)memory_state.limit);
    trace_str += format("memory_state.needed\t%lld\n", (Lld)memory_state.needed);
  }

  {
    uint64_t max_memory = 0;
    uint64_t available_memory = 0;
    uint64_t accesses = 0;
    uint64_t hits = 0;
    if (Global::block_cache)
      Global::block_cache->get_stats(&max_memory, &available_memory, &accesses, &hits);
    if (debug) {
      trace_str += format("FileBlockCache-max_memory\t%llu\n", (Llu)max_memory);
      trace_str += format("FileBlockCache-available_memory\t%llu\n", (Llu)available_memory);
      trace_str += format("FileBlockCache-accesses\t%llu\n", (Llu)accesses);
      trace_str += format("FileBlockCache-hits\t%llu\n", (Llu)hits);
    }
  }

  if (!do_scheduling)
    return;

  // Drop all outstanding range tasks from maintenance queue
  Global::maintenance_queue->drop_range_tasks([](Range *) -> bool {return true;});

  StringSet remove_ok_logs, removed_logs;
  m_live_map->get_ranges(ranges, &remove_ok_logs);
  time_t current_time = time(0);
  int flags = 0;
  /// Update current time to compute "low activity" window
  if (Global::low_activity_time.update_current_time()) {
    if (Global::low_activity_time.is_window_enabled())
      flags = MaintenanceFlag::RECOMPUTE_MERGE_RUN;
    HT_INFOF("%s low activity window", Global::low_activity_time.within_window()
             ? "Entering" : "Exiting");
  }
  if (debug)
    trace_str += 
      format("Within low activity window = %s\n",
             Global::low_activity_time.within_window() ? "true" : "false");

  // Fetch maintenance data for ranges and thier access groups
  for (auto &rd : ranges.array)
    rd.data = rd.range->get_maintenance_data(ranges.arena, current_time, flags);

  if (ranges.array.empty()) {
    if (!Global::range_initialization_complete)
      Global::range_initialization_complete = true;
    return;
  }

  // Make a copy of the range statistics array for get_statistics()
  {
    RangesPtr ranges_copy = make_shared<Ranges>();
    ranges_copy->array = ranges.array;
    for (size_t i=0; i<ranges.array.size(); i++) {
      ranges_copy->array[i].data =
        (Range::MaintenanceData *)ranges_copy->arena.alloc( sizeof(Range::MaintenanceData) );
      memcpy(ranges_copy->array[i].data, ranges.array[i].data, sizeof(Range::MaintenanceData));
      ranges_copy->array[i].data->agdata = 0;
    }
    Global::set_ranges(ranges_copy);
  }

  // Rotate the starting point to avoid compaction starvation during high write
  // activity with many ranges.
  if (!low_memory) {
    m_start_offset %= ranges.array.size();
    if (m_start_offset != 0) {
      std::vector<RangeData> rotated;
      rotated.reserve(ranges.array.size());
      std::vector<RangeData>::iterator iter = ranges.array.begin() + m_start_offset;
      rotated.insert(rotated.end(), iter, ranges.array.end());
      rotated.insert(rotated.end(), ranges.array.begin(), iter);
      ranges.array.swap(rotated);
    }
    m_start_offset += m_maintenance_queue_worker_count;
  }

  // Remove ranges in table blacklist
  {
    lock_guard<mutex> lock(m_mutex);
    if (!m_table_blacklist.empty())
      ranges.remove_if(in_blacklist);
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

    if (debug) {
      trace_str += format("before revision_root\t%llu\n", (Llu)revision_root);
      trace_str += format("before revision_metadata\t%llu\n", (Llu)revision_metadata);
      trace_str += format("before revision_system\t%llu\n", (Llu)revision_system);
      trace_str += format("before revision_user\t%llu\n", (Llu)revision_user);
    }

    for (auto &rd : ranges.array) {

      if (rd.data->needs_major_compaction && priority <= m_move_compactions_per_interval) {
        rd.data->priority = priority++;
        rd.data->maintenance_flags = MaintenanceFlag::COMPACT_MOVE;
      }

      if (!rd.data->load_acknowledged)
        not_acknowledged++;

      for (ag_data = rd.data->agdata; ag_data; ag_data = ag_data->next) {

        // compute memory stats
        cell_cache_memory += ag_data->mem_allocated;
        for (cs_data = ag_data->csdata; cs_data; cs_data = cs_data->next) {
          shadow_cache_memory += cs_data->shadow_cache_size;
          block_index_memory += cs_data->index_stats.block_index_memory;
          bloom_filter_memory += cs_data->index_stats.bloom_filter_memory;
        }

        if (ag_data->earliest_cached_revision != TIMESTAMP_MAX) {
          if (rd.range->is_root()) {
            if (ag_data->earliest_cached_revision < revision_root)
              revision_root = ag_data->earliest_cached_revision;
          }
          else if (rd.data->is_metadata) {
            if (ag_data->earliest_cached_revision < revision_metadata)
              revision_metadata = ag_data->earliest_cached_revision;
          }
          else if (rd.data->is_system) {
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

    if (debug) {
      trace_str += format("after revision_root\t%llu\n", (Llu)revision_root);
      trace_str += format("after revision_metadata\t%llu\n", (Llu)revision_metadata);
      trace_str += format("after revision_system\t%llu\n", (Llu)revision_system);
      trace_str += format("after revision_user\t%llu\n", (Llu)revision_user);
    }

    if (Global::root_log)
      Global::root_log->purge(revision_root, remove_ok_logs, removed_logs, &trace_str);

    if (Global::metadata_log)
      Global::metadata_log->purge(revision_metadata, remove_ok_logs, removed_logs, &trace_str);

    if (Global::system_log)
      Global::system_log->purge(revision_system, remove_ok_logs, removed_logs, &trace_str);

    if (Global::user_log)
      Global::user_log->purge(revision_user, remove_ok_logs, removed_logs, &trace_str);

    // Remove logs that were removed from the MetaLogEntityRemoveOkLogs entity
    if (!removed_logs.empty()) {
      Global::remove_ok_logs->remove(removed_logs);
      Global::rsml_writer->record_state(Global::remove_ok_logs);
    }
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
             (double)memory_state.balance/(double)MiB, (double)total_memory/(double)MiB,
             (double)Global::memory_limit/(double)MiB);
    HT_INFOF("Memory Allocation: BlockCache=%.2f%% BlockIndex=%.2f%% "
             "BloomFilter=%.2f%% CellCache=%.2f%% ShadowCache=%.2f%% "
             "QueryCache=%.2f%%",
             block_cache_pct, block_index_pct, bloom_filter_pct,
             cell_cache_pct, shadow_cache_pct, query_cache_pct);
  }

  if (debug)
    trace_str += "\nScheduling Decisions:\n";

  m_prioritizer->prioritize(ranges.array, memory_state, priority,
                            debug ? &trace_str : 0);

  if (debug)
    write_debug_output(now, ranges, trace_str);

  auto schedule_time = chrono::steady_clock::now();

  if (not_acknowledged) {
    HT_INFOF("Found load_acknowledged=false in %d ranges", (int)not_acknowledged);
  }

  bool uninitialized_range_seen {};

  // if this is the first time around, just enqueue work that
  // was in progress
  if (!m_initialized) {
    uint32_t level = 0, priority = 0;
    for (auto &rd : ranges.array) {
      if (!rd.data->initialized)
        uninitialized_range_seen = true;
      if (rd.data->state == RangeState::SPLIT_LOG_INSTALLED ||
          rd.data->state == RangeState::SPLIT_SHRUNK) {
        level = get_level(rd);
        Global::maintenance_queue->add(new MaintenanceTaskSplit(level, priority++, schedule_time, rd.range));
      }
      else if (rd.data->state == RangeState::RELINQUISH_LOG_INSTALLED) {
        level = get_level(rd);
        Global::maintenance_queue->add(new MaintenanceTaskRelinquish(level, priority++, schedule_time, rd.range));
      }
    }
    m_initialized = true;
  }
  else {

    lock_guard<mutex> lock(m_mutex);

    // Remove ranges for tables in blacklist
    if (!m_table_blacklist.empty())
      ranges.remove_if(in_blacklist);

    // Sort the ranges based on priority
    ranges_prioritized.array.reserve( ranges.array.size() );
    for (auto &rd : ranges.array) {
      if (rd.data->priority > 0)
        ranges_prioritized.array.push_back(rd);
    }
    struct RangeDataAscending ordering;
    sort(ranges_prioritized.array.begin(), ranges_prioritized.array.end(), ordering);

    int32_t merges_created = 0;
    int32_t initialization_created = 0;
    uint32_t level = 0;

    for (auto &rd : ranges_prioritized.array) {
      if (!rd.data->initialized) {
        uninitialized_range_seen = true;
        if (!low_memory && initialization_created < m_initialization_per_interval) {
          level = get_level(rd);
          Global::maintenance_queue->add(new MaintenanceTaskDeferredInitialization(
                                         level, rd.data->priority,
                                         schedule_time, rd.range));
          ++initialization_created;
        }
      }
      if (rd.data->maintenance_flags & MaintenanceFlag::SPLIT) {
        level = get_level(rd);
        Global::maintenance_queue->add(new MaintenanceTaskSplit(level, rd.data->priority,
                                                                schedule_time, rd.range));
      }
      else if (rd.data->maintenance_flags & MaintenanceFlag::RELINQUISH) {
        level = get_level(rd);
        Global::maintenance_queue->add(new MaintenanceTaskRelinquish(level, rd.data->priority,
                                                                     schedule_time, rd.range));
      }
      else if (rd.data->maintenance_flags & MaintenanceFlag::COMPACT) {
        MaintenanceTaskCompaction *task;
        level = get_level(rd);
        task = new MaintenanceTaskCompaction(level, rd.data->priority,
                                             schedule_time, rd.range);
        if (!rd.data->needs_major_compaction) {
          for (AccessGroup::MaintenanceData *ag_data=rd.data->agdata; ag_data; ag_data=ag_data->next) {
            if (MaintenanceFlag::minor_compaction(ag_data->maintenance_flags) ||
                MaintenanceFlag::major_compaction(ag_data->maintenance_flags) ||
                MaintenanceFlag::gc_compaction(ag_data->maintenance_flags))
              task->add_subtask(ag_data->ag, ag_data->maintenance_flags);
            else if (MaintenanceFlag::merging_compaction(ag_data->maintenance_flags)) {
              if (merges_created < m_merges_per_interval) {
                task->add_subtask(ag_data->ag, ag_data->maintenance_flags);
                merges_created++;
              }
            }
          }
        }
        Global::maintenance_queue->add(task);
      }
      else if (rd.data->maintenance_flags & MaintenanceFlag::MEMORY_PURGE) {
        MaintenanceTaskMemoryPurge *task;
        level = get_level(rd);
        task = new MaintenanceTaskMemoryPurge(level, rd.data->priority,
                                              schedule_time, rd.range);
        for (AccessGroup::MaintenanceData *ag_data=rd.data->agdata; ag_data; ag_data=ag_data->next) {
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

  if (!Global::range_initialization_complete && !uninitialized_range_seen)
    Global::range_initialization_complete = true;

  MaintenanceTaskWorkQueue *task = 0;
  {
    lock_guard<mutex>  lock(Global::mutex);
    if (!Global::work_queue.empty())
      task = new MaintenanceTaskWorkQueue(3, 0, Global::work_queue);
  }
  if (task)
    Global::maintenance_queue->add(task);

  //cout << flush << trace_str << flush;
}

int MaintenanceScheduler::get_level(RangeData &rd) {
  if (rd.range->is_root())
    return 0;
  if (rd.data->is_metadata)
    return 1;
  else if (rd.data->is_system)
    return 2;
  return 3;
}


bool MaintenanceScheduler::debug_signal_file_exists(chrono::steady_clock::time_point now) {
  if (now - m_last_check >= chrono::milliseconds(60000)) {
    m_last_check = now;
    return FileUtils::exists(System::install_dir + "/run/debug-scheduler");
  }
  return false;
}


void MaintenanceScheduler::write_debug_output(chrono::steady_clock::time_point now,
                                              Ranges &ranges,
                                              const String &header_str) {
  AccessGroup::MaintenanceData *ag_data;
  String output_fname = System::install_dir + "/run/scheduler.output";
  ofstream out;
  out.open(output_fname.c_str());
  out << header_str << "\n";
  for (auto &rd : ranges.array) {
    out << *rd.data << "\n";
    for (ag_data = rd.data->agdata; ag_data; ag_data = ag_data->next)
      out << *ag_data << "\n";
  }
  StringSet logs;
  Global::remove_ok_logs->get(logs);
  out << "RemoveOkLogs:\n";
  for (const auto &log : logs)
    cout << log << "\n";
  out.close();
  FileUtils::unlink(System::install_dir + "/run/debug-scheduler");
}
