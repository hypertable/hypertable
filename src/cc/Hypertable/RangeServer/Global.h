/* -*- c++ -*-
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

#ifndef HYPERTABLE_RANGESERVER_GLOBAL_H
#define HYPERTABLE_RANGESERVER_GLOBAL_H

#include <string>

#include <boost/thread/thread.hpp>

#include "Common/Properties.h"
#include "Common/Filesystem.h"
#include "Common/TimeWindow.h"

#include "AsyncComm/Comm.h"
#include "AsyncComm/ApplicationQueue.h"
#include "Hyperspace/Session.h"
#include "Hypertable/Lib/CommitLog.h"
#include "Hypertable/Lib/Master/Client.h"
#include "Hypertable/Lib/MetaLogWriter.h"
#include "Hypertable/Lib/PseudoTables.h"
#include "Hypertable/Lib/Schema.h"
#include "Hypertable/Lib/Client.h"
#include "Hypertable/Lib/RangeSpec.h"
#include "Hypertable/Lib/TableIdentifier.h"

#include "FileBlockCache.h"
#include "LoadStatistics.h"
#include "LocationInitializer.h"
#include "MaintenanceQueue.h"
#include "MemoryTracker.h"
#include "MetaLogEntityTask.h"
#include "MetaLogEntityRemoveOkLogs.h"
#include "TableInfo.h"

#include <mutex>

namespace Hypertable {

  using namespace Lib;

  class Global {
  public:
    static std::mutex mutex;
    static Hyperspace::SessionPtr hyperspace;
    static Hypertable::FilesystemPtr dfs;
    static Hypertable::FilesystemPtr log_dfs;
    static Hypertable::ApplicationQueuePtr app_queue;
    static Hypertable::MaintenanceQueuePtr maintenance_queue;
    static Hypertable::Lib::Master::ClientPtr master_client;
    static Hypertable::RangeLocatorPtr range_locator;
    static Hypertable::PseudoTables *pseudo_tables;
    static MetaLogEntityRemoveOkLogsPtr remove_ok_logs;
    static LoadStatisticsPtr load_statistics;
    static RangesPtr      ranges;
    static gBoolPtr       verbose;
    static bool           auto_re_init_location;
    static bool           row_size_unlimited;
    static bool           ignore_cells_with_clock_skew;
    static CommitLogPtr user_log;
    static CommitLogPtr system_log;
    static CommitLogPtr metadata_log;
    static CommitLogPtr root_log;
    static MetaLog::WriterPtr rsml_writer;
    static std::string    log_dir;
    static LocationInitializerPtr location_initializer;
    static int64_t        range_split_size;
    static int64_t        range_maximum_size;
    static int32_t        failover_timeout;
    static int32_t        access_group_garbage_compaction_threshold;
    static int32_t        access_group_max_mem;
    static int32_t        cell_cache_scanner_cache_size;
    static Hypertable::FileBlockCache *block_cache;
    static TablePtr       metadata_table;
    static TablePtr       rs_metrics_table;
    static int64_t        range_metadata_split_size;
    static Hypertable::MemoryTracker *memory_tracker;
    static int64_t        log_prune_threshold_min;
    static int64_t        log_prune_threshold_max;
    static int64_t        cellstore_target_size_min;
    static int64_t        cellstore_target_size_max;
    static int64_t        memory_limit;
    // amount of unused physical memory to achieve according
    // to the configuration
    static int64_t        memory_limit_ensure_unused;
    // amount of unused physical memory to achieve according
    // to the current memory situation
    static int64_t        memory_limit_ensure_unused_current;
    static uint64_t       access_counter;
    static bool           enable_shadow_cache;
    static std::string    toplevel_dir;
    static int32_t        metrics_interval;
    static int32_t        merge_cellstore_run_length_threshold;
    static bool           ignore_clock_skew_errors;
    static bool           range_initialization_complete;
    static ConnectionManagerPtr conn_manager;
    static std::vector<MetaLog::EntityTaskPtr> work_queue;
    static StringSet      immovable_range_set;
    static TimeWindow low_activity_time;
    static void add_to_work_queue(MetaLog::EntityTaskPtr entity);
    static void immovable_range_set_add(const TableIdentifier &table, const RangeSpec &spec);
    static void immovable_range_set_remove(const TableIdentifier &table, const RangeSpec &spec);
    static bool immovable_range_set_contains(const TableIdentifier &table, const RangeSpec &spec);
    static void set_ranges(RangesPtr &r);
    static RangesPtr get_ranges();
  };

} // namespace Hypertable

#endif // HYPERTABLE_RANGESERVER_GLOBAL_H
