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
#include <cassert>

#include "AsyncComm/ApplicationQueue.h"

#include "Global.h"

using namespace Hypertable;
using namespace Hyperspace;

namespace Hypertable {

  Mutex                  Global::mutex;
  SessionPtr             Global::hyperspace = 0;
  FilesystemPtr          Global::dfs;
  FilesystemPtr          Global::log_dfs;
  MaintenanceQueuePtr    Global::maintenance_queue;
  MasterClientPtr        Global::master_client;
  RangeServerProtocol   *Global::protocol = 0;
  RangeLocatorPtr        Global::range_locator = 0;
  bool                   Global::verbose = false;
  CommitLog             *Global::user_log = 0;
  CommitLog             *Global::system_log = 0;
  CommitLog             *Global::metadata_log = 0;
  CommitLog             *Global::root_log = 0;
  MetaLog::WriterPtr     Global::rsml_writer;
  std::string            Global::log_dir = "";
  LocationInitializerPtr Global::location_initializer;
  int64_t                Global::range_split_size = 0;
  int64_t                Global::range_maximum_size = 0;
  int32_t                Global::access_group_garbage_compaction_threshold = 0;
  int32_t                Global::access_group_max_mem = 0;
  int32_t                Global::cell_cache_scanner_cache_size = 0;
  ScannerMap             Global::scanner_map;
  FileBlockCache        *Global::block_cache = 0;
  TablePtr               Global::metadata_table = 0;
  TablePtr               Global::rs_metrics_table = 0;
  int64_t                Global::range_metadata_split_size = 0;
  MemoryTracker         *Global::memory_tracker = 0;
  int64_t                Global::log_prune_threshold_min = 0;
  int64_t                Global::log_prune_threshold_max = 0;
  int64_t                Global::cellstore_target_size_min = 0;
  int64_t                Global::cellstore_target_size_max = 0;
  int64_t                Global::memory_limit = 0;
  int64_t                Global::memory_limit_ensure_unused = 0;
  int64_t                Global::memory_limit_ensure_unused_current = 0;
  uint64_t               Global::access_counter = 0;
  bool                   Global::enable_shadow_cache = true;
  std::string            Global::toplevel_dir;
  int32_t                Global::metrics_interval = 0;
  int32_t                Global::merge_cellstore_run_length_threshold = 0;
  bool                   Global::ignore_clock_skew_errors = false;
  ConnectionManagerPtr   Global::conn_manager;
  std::vector<MetaLog::EntityTaskPtr>  Global::work_queue;
  StringSet              Global::immovable_range_set;

  void Global::immovable_range_set_add(const TableIdentifier &table, const RangeSpec &spec) {
    ScopedLock lock(Global::mutex);
    String name = format("%s[%s..%s]", table.id, spec.start_row, spec.end_row);
    HT_ASSERT(Global::immovable_range_set.count(name) == 0);
    Global::immovable_range_set.insert(name);
  }

  void Global::immovable_range_set_remove(const TableIdentifier &table, const RangeSpec &spec) {
    ScopedLock lock(Global::mutex);
    String name = format("%s[%s..%s]", table.id, spec.start_row, spec.end_row);
    HT_ASSERT(Global::immovable_range_set.count(name) == 1);
    Global::immovable_range_set.erase(name);
  }

  bool Global::immovable_range_set_contains(const TableIdentifier &table, const RangeSpec &spec) {
    ScopedLock lock(Global::mutex);
    String name = format("%s[%s..%s]", table.id, spec.start_row, spec.end_row);
    return Global::immovable_range_set.count(name) > 0;
  }

}
