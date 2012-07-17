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

#ifndef HYPERTABLE_MAINTENANCESCHEDULER_H
#define HYPERTABLE_MAINTENANCESCHEDULER_H

#include "Common/ReferenceCount.h"

#include "MaintenancePrioritizerLogCleanup.h"
#include "MaintenancePrioritizerLowMemory.h"
#include "RSStats.h"

namespace Hypertable {

  class MaintenanceScheduler : public ReferenceCount {
  public:
    MaintenanceScheduler(MaintenanceQueuePtr &queue, RSStatsPtr &server_stats,
                         RangeStatsGathererPtr &gatherer);

    void schedule();

    void set_low_memory_mode(bool on) {
      if (on) {
        if (m_prioritizer != &m_prioritizer_low_memory)
          m_prioritizer = &m_prioritizer_low_memory;
      }
      else {
        if (m_prioritizer != &m_prioritizer_log_cleanup) {
          m_prioritizer = &m_prioritizer_log_cleanup;
          boost::xtime_get(&m_last_low_memory, TIME_UTC);
        }
      }
    }

    void need_scheduling() {
      m_scheduling_needed = true;
    }

  private:

    int get_level(Range::MaintenanceData *range_data);

    bool low_memory_mode() {
      return m_prioritizer == &m_prioritizer_low_memory;
    }

    void check_file_dump_statistics(boost::xtime now, RangeStatsVector &range_data,
                                    const String &header_str);

    bool m_initialized;
    bool m_scheduling_needed;
    ApplicationQueuePtr m_app_queue;
    MaintenanceQueuePtr m_queue;
    RSStatsPtr m_server_stats;
    RangeStatsGathererPtr m_stats_gatherer;
    MaintenancePrioritizer *m_prioritizer;
    MaintenancePrioritizerLogCleanup m_prioritizer_log_cleanup;
    MaintenancePrioritizerLowMemory  m_prioritizer_low_memory;
    int32_t m_maintenance_interval;
    boost::xtime m_last_maintenance;
    boost::xtime m_last_low_memory;
    boost::xtime m_last_check;
    int64_t m_query_cache_memory;
    int32_t m_low_memory_limit_percentage;
    int32_t m_merging_delay;
    int32_t m_merges_per_interval;
    int32_t m_move_compactions_per_interval;
    std::set<int64_t> m_log_hashes;
  };

  typedef intrusive_ptr<MaintenanceScheduler> MaintenanceSchedulerPtr;


}

#endif // HYPERTABLE_MAINTENANCESCHEDULER_H


