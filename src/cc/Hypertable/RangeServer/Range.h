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

#ifndef HYPERTABLE_RANGE_H
#define HYPERTABLE_RANGE_H

#include <map>
#include <vector>

#include "Common/Barrier.h"
#include "Common/String.h"

#include "Hypertable/Lib/CommitLog.h"
#include "Hypertable/Lib/CommitLogReader.h"
#include "Hypertable/Lib/Key.h"
#include "Hypertable/Lib/MasterClient.h"
#include "Hypertable/Lib/RangeState.h"
#include "Hypertable/Lib/Schema.h"
#include "Hypertable/Lib/Timestamp.h"
#include "Hypertable/Lib/Types.h"

#include "AccessGroup.h"
#include "CellStore.h"
#include "LoadFactors.h"
#include "LoadMetricsRange.h"
#include "MaintenanceFlag.h"
#include "MetaLogEntityRange.h"
#include "MetaLogEntityTask.h"
#include "Metadata.h"
#include "RangeMaintenanceGuard.h"
#include "RangeSet.h"
#include "RangeTransferInfo.h"

namespace Hypertable {

  /**
   * Represents a table row range.
   */
  class Range : public CellList {

  public:

    class MaintenanceData {
    public:
      int64_t compactable_memory() {
        int64_t total = 0;
        for (AccessGroup::MaintenanceData *ag = agdata; ag; ag = ag->next)
          total += ag->mem_used;
        return total;
      }
      Range *range;
      AccessGroup::MaintenanceData *agdata;
      const char *table_id;
      LoadFactors load_factors;
      uint64_t cells_returned;
      uint64_t bytes_returned;
      int64_t  purgeable_index_memory;
      int64_t  compact_memory;
      int64_t soft_limit;
      uint32_t schema_generation;
      int32_t  priority;
      int16_t  state;
      int16_t  maintenance_flags;
      uint32_t file_count;
      uint64_t cell_count;
      uint64_t memory_used;
      uint64_t memory_allocated;
      int64_t key_bytes;
      int64_t value_bytes;
      double compression_ratio;
      uint64_t disk_used;
      uint64_t disk_estimate;
      uint64_t shadow_cache_memory;
      uint64_t block_index_memory;
      uint64_t bloom_filter_memory;
      uint32_t bloom_filter_accesses;
      uint32_t bloom_filter_maybes;
      uint32_t bloom_filter_fps;
      bool     busy;
      bool     is_metadata;
      bool     is_system;
      bool     relinquish;
      bool     needs_major_compaction;
      bool     needs_split;
      int64_t  log_hash;
      bool     load_acknowledged;
    };

    typedef std::map<String, AccessGroup *> AccessGroupMap;
    typedef std::vector<AccessGroupPtr> AccessGroupVector;

    Range(MasterClientPtr &, const TableIdentifier *, SchemaPtr &,
          const RangeSpec *, RangeSet *, const RangeState *, bool needs_compaction=false);
    Range(MasterClientPtr &, SchemaPtr &, MetaLog::EntityRange *, RangeSet *);
    virtual ~Range() {}
    virtual void add(const Key &key, const ByteString value);
    virtual const char *get_split_row() { return 0; }

    virtual int64_t get_total_entries() {
      boost::mutex::scoped_lock lock(m_mutex);
      int64_t total = 0;
      for (size_t i=0; i<m_access_group_vector.size(); i++)
        total += m_access_group_vector[i]->get_total_entries();
      return total;
    }

    void lock();
    void unlock();

    uint64_t disk_usage();

    CellListScanner *create_scanner(ScanContextPtr &scan_ctx);

    String start_row() {
      ScopedLock lock(m_mutex);
      return m_metalog_entity->spec.start_row;
    }

    const char *start_row_cstring() const {
      return m_metalog_entity->spec.start_row;
    }

    /**
     * Returns the end row of the range.
     */
    String end_row() {
      ScopedLock lock(m_mutex);
      return m_metalog_entity->spec.end_row;
    }

    const char *end_row_cstring() {
      return m_metalog_entity->spec.end_row;
    }

    int64_t get_scan_revision();

    void replay_transfer_log(CommitLogReader *commit_log_reader);

    MaintenanceData *get_maintenance_data(ByteArena &arena, time_t now, TableMutator *mutator);

    void wait_for_maintenance_to_complete() {
      m_maintenance_guard.wait_for_complete();
    }

    void update_schema(SchemaPtr &schema);

    void split();

    void relinquish();

    void compact(MaintenanceFlag::Map &subtask_map);

    void purge_memory(MaintenanceFlag::Map &subtask_map);

    void schedule_relinquish() { m_relinquish = true; }
    bool get_relinquish() const { return m_relinquish; }

    void recovery_initialize() {
      ScopedLock lock(m_mutex);
      for (size_t i=0; i<m_access_group_vector.size(); i++)
        m_access_group_vector[i]->recovery_initialize();
    }

    void recovery_finalize();

    bool increment_update_counter() {
      if (m_dropped)
        return false;
      m_update_barrier.enter();
      if (m_dropped) {
        m_update_barrier.exit();
        return false;
      }
      return true;
    }
    void decrement_update_counter() {
      m_update_barrier.exit();
    }

    bool increment_scan_counter() {
      if (m_dropped)
        return false;
      m_scan_barrier.enter();
      if (m_dropped) {
        m_scan_barrier.exit();
        return false;
      }
      return true;
    }
    void decrement_scan_counter() {
      m_scan_barrier.exit();
    }

    /**
     * @param transfer_info
     * @param split_log
     * @param latest_revisionp
     * @param wait_for_maintenance true if this range has exceeded its capacity and
     *        future requests to this range need to be throttled till split/compaction reduces
     *        range size
     * @return true if transfer log installed
     */
    bool get_transfer_info(RangeTransferInfo &transfer_info, CommitLogPtr &transfer_log,
                           int64_t *latest_revisionp, bool &wait_for_maintenance) {
      bool retval = false;
      ScopedLock lock(m_mutex);

      wait_for_maintenance = false;
      *latest_revisionp = m_latest_revision;

      if (m_transfer_log) {
        transfer_log = m_transfer_log;
        retval = true;
      }

      if (m_split_row.length())
        transfer_info.set_split(m_split_row, m_split_off_high);
      else
        transfer_info.clear();

      if (m_capacity_exceeded_throttle == true)
        wait_for_maintenance = true;

      return retval;
    }

    void add_read_data(uint64_t cells_scanned, uint64_t cells_returned,
                       uint64_t bytes_scanned, uint64_t bytes_returned,
                       uint64_t disk_bytes_read) {
      m_cells_scanned += cells_scanned;
      m_cells_returned += cells_returned;
      m_bytes_scanned += bytes_scanned;
      m_bytes_returned += bytes_returned;
      m_disk_bytes_read += disk_bytes_read;
    }

    void add_bytes_written(uint64_t n) {
      m_bytes_written += n;
    }

    void add_cells_written(uint64_t n) {
      m_cells_written += n;
    }

    bool need_maintenance();

    bool is_root() { return m_is_root; }

    void drop() {
      Barrier::ScopedActivator block_updates(m_update_barrier);
      Barrier::ScopedActivator block_scans(m_scan_barrier);
      ScopedLock lock(m_mutex);
      m_dropped = true;
    }

    String get_name() {
      ScopedLock lock(m_mutex);
      return (String)m_name;
    }

    /**
     * Only called during startup (before range is live) so locking is not
     * necessary
     */
    int get_state() {
      return m_metalog_entity->state.state;
    }

    int32_t get_error() { return m_error; }

    void set_needs_compaction(bool needs_compaction) {
      ScopedLock lock(m_mutex);
      m_metalog_entity->needs_compaction = needs_compaction;
    }

    bool get_needs_compaction() {
      ScopedLock lock(m_mutex);
      return m_metalog_entity->needs_compaction;
    }

    void acknowledge_load();

    bool load_acknowledged() {
      // Not locking this mutex for performance reasons.  Ranges start out
      // in life unacknowledged and then quickly become acknowledged.
      // There is a potential race condition at the brief time this variable
      // is set, but it should not cause any logic problems and saves
      // us the lock cost every time this method is called.
      //ScopedLock lock(m_mutex);
      return m_metalog_entity->load_acknowledged;
    }

    void record_state_rsml();

    void record_removal_rsml();

  private:

    void initialize();

    void load_cell_stores(Metadata *metadata);

    bool cancel_maintenance();

    void relinquish_install_log();
    void relinquish_compact_and_finish();

    bool determine_split_row_from_cached_keys(AccessGroupVector &ag_vector);

    void split_install_log();
    void split_compact_and_shrink();
    void split_notify_master();

    void split_install_log_rollback_metadata();

    void maybe_create_log_removal_task(MetaLog::EntityTaskPtr &log_removal_task);

    // these need to be aligned
    uint64_t         m_scans;
    uint64_t         m_cells_scanned;
    uint64_t         m_cells_returned;
    uint64_t         m_cells_written;
    uint64_t         m_updates;
    uint64_t         m_bytes_scanned;
    uint64_t         m_bytes_returned;
    uint64_t         m_bytes_written;
    uint64_t         m_disk_bytes_read;

    Mutex            m_mutex;
    Mutex            m_schema_mutex;
    MasterClientPtr  m_master_client;
    MetaLog::EntityRangePtr m_metalog_entity;
    SchemaPtr        m_schema;
    String           m_name;
    AccessGroupMap     m_access_group_map;
    AccessGroupVector  m_access_group_vector;
    std::vector<AccessGroup *>       m_column_family_vector;
    RangeMaintenanceGuard m_maintenance_guard;
    int64_t          m_revision;
    int64_t          m_latest_revision;
    int64_t          m_split_threshold;
    String           m_split_row;
    CommitLogPtr     m_transfer_log;
    bool             m_split_off_high;
    Barrier          m_update_barrier;
    Barrier          m_scan_barrier;
    bool             m_is_root;
    uint64_t         m_added_deletes[KEYSPEC_DELETE_MAX];
    uint64_t         m_added_inserts;
    RangeSet        *m_range_set;
    int32_t          m_error;
    bool             m_dropped;
    bool             m_capacity_exceeded_throttle;
    bool             m_relinquish;
    bool             m_removed_from_working_set;
    int64_t          m_maintenance_generation;
    LoadMetricsRange m_load_metrics;
    int64_t          m_log_hash;
  };

  typedef intrusive_ptr<Range> RangePtr;

  std::ostream &operator<<(std::ostream &os, const Range::MaintenanceData &mdata);

} // namespace Hypertable

#endif // HYPERTABLE_RANGE_H
