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

/** @file
 * Definitions for TimerHandler.
 * This file contains type declarations for TimerHandler, a class for
 * managing the maintenance timer.
 */

#include <Common/Compat.h>

#include "Global.h"
#include "RangeServer.h"
#include "Request/Handler/DoMaintenance.h"
#include "TimerHandler.h"

#include <Hypertable/Lib/KeySpec.h>

#include <Common/Config.h>
#include <Common/Error.h>
#include <Common/InetAddr.h>
#include <Common/StringExt.h>
#include <Common/System.h>
#include <Common/Time.h>

#include <algorithm>
#include <sstream>

using namespace Hypertable;
using namespace Hypertable::RangeServer;
using namespace Hypertable::Config;

TimerHandler::TimerHandler(Comm *comm, Apps::RangeServer *range_server)
  : m_comm(comm), m_range_server(range_server) {
  int32_t maintenance_interval;

  m_query_cache_memory = get_i64("Hypertable.RangeServer.QueryCache.MaxMemory");
  m_timer_interval = get_i32("Hypertable.RangeServer.Timer.Interval");
  maintenance_interval = get_i32("Hypertable.RangeServer.Maintenance.Interval");
  m_userlog_size_threshold = (int64_t)((double)Global::log_prune_threshold_max * 1.2);
  m_max_app_queue_pause = get_i32("Hypertable.RangeServer.Maintenance.MaxAppQueuePause");

  if (m_timer_interval > (maintenance_interval+10)) {
    m_timer_interval = maintenance_interval + 10;
    HT_INFOF("Reducing timer interval to %d to support maintenance interval %d",
             m_timer_interval, maintenance_interval);
  }

  m_current_interval = m_timer_interval;

  m_last_schedule = std::chrono::steady_clock::now();

  m_app_queue = m_range_server->get_application_queue();

  return;
}



void TimerHandler::start() {
  int error;
  if ((error = m_comm->set_timer(0, shared_from_this())) != Error::OK)
    HT_FATALF("Problem setting timer - %s", Error::get_text(error));
}


void TimerHandler::schedule_immediate_maintenance() {
  lock_guard<mutex> lock(m_mutex);

  if (!m_immediate_maintenance_scheduled && !m_schedule_outstanding) {
    auto now = std::chrono::steady_clock::now();
    uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_schedule).count();
    int error;
    uint32_t millis = (elapsed < 1000) ? 1000 - elapsed : 0;
    HT_INFOF("Scheduling immediate maintenance for %u millis in the future", millis);
    if ((error = m_comm->set_timer(millis, shared_from_this())) != Error::OK)
      HT_FATALF("Problem setting timer - %s", Error::get_text(error));
    m_immediate_maintenance_scheduled = true;
  }
  return;
}


void TimerHandler::maintenance_scheduled_notify() {
  lock_guard<mutex> lock(m_mutex);

  m_schedule_outstanding = false;
  m_last_schedule = std::chrono::steady_clock::now();

  if (m_range_server->replay_finished()) {
    if (Global::user_log && Global::user_log->size()>m_userlog_size_threshold) {
      if (!m_app_queue_paused)
        pause_app_queue();
    }
    else if (m_app_queue_paused && !low_memory())
      restart_app_queue();
  }

  if (m_immediate_maintenance_scheduled)
    m_immediate_maintenance_scheduled = false;
  else {
    int error;
    if ((error = m_comm->set_timer(m_current_interval, shared_from_this())) != Error::OK)
      HT_FATALF("Problem setting timer - %s", Error::get_text(error));
  }
}


void TimerHandler::shutdown() {
  lock_guard<mutex> lock(m_mutex);
  m_shutdown = true;
  m_comm->cancel_timer(shared_from_this());
}


void TimerHandler::handle(Hypertable::EventPtr &event) {
  lock_guard<mutex> lock(m_mutex);
  int error;
  bool do_maintenance = !m_schedule_outstanding;

  if (m_shutdown)
    return;

  auto now = std::chrono::steady_clock::now();

  if (m_app_queue_paused) {
    unsigned int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_pause_time).count();
    HT_DEBUGF("App queue paused (pause_time=%u, lmm=%s, restart_gen=%lld, "
              "queue_gen=%lld)", (unsigned)elapsed,
              m_low_memory_mode ? "true" : "false", (Lld)m_restart_generation,
              (Lld)Global::maintenance_queue->generation());
    if ((m_low_memory_mode && !low_memory()) ||
        (now - m_pause_time) >= std::chrono::milliseconds(m_max_app_queue_pause) ||
        m_restart_generation <= Global::maintenance_queue->generation())
      restart_app_queue();
    do_maintenance = !m_schedule_outstanding && m_immediate_maintenance_scheduled;
  }
  else {
    if (low_memory()) {
      if (m_low_memory_mode)
        pause_app_queue();
      else
        m_low_memory_mode = true;
    }
    else {
      m_low_memory_mode = false;
      if (Global::user_log && Global::user_log->size()>m_userlog_size_threshold)
        pause_app_queue();
    }
  }

  // If immediate maintenance requested, disable low memory mode
  if (m_immediate_maintenance_scheduled)
    m_low_memory_mode = false;

  HT_DEBUGF("aq_paused=%s, lowmm=%s, ci=%d, ims=%s, so=%s, dm=%s",
            m_app_queue_paused ? "true" : "false",
            m_low_memory_mode ? "true" : "false",
            (int)m_current_interval,
            m_immediate_maintenance_scheduled ? "true" : "false",
            m_schedule_outstanding ? "true" : "false",
            do_maintenance ? "true" : "false");

  try {

    if (event->type == Hypertable::Event::TIMER) {

      if (do_maintenance) {
        m_app_queue->add( new Request::Handler::DoMaintenance(m_range_server) );
        m_schedule_outstanding = true;
      }
      else {
        if ((error = m_comm->set_timer(m_current_interval, shared_from_this())) != Error::OK)
          HT_FATALF("Problem setting timer - %s", Error::get_text(error));
      }
    }
    else
      HT_ERRORF("Unexpected event - %s", event->to_str().c_str());
  }
  catch (Hypertable::Exception &e) {
    HT_ERROR_OUT << e << HT_END;
  }
}

void TimerHandler::pause_app_queue() {
  m_app_queue->stop();
  m_app_queue_paused = true;
  m_current_interval = 500;
  m_restart_generation = Global::maintenance_queue->generation() +
    Global::maintenance_queue->size();
  HT_INFOF("Application queue PAUSED due to %s",
           m_low_memory_mode ? "low memory" : "log size threshold exceeded");
  m_pause_time = std::chrono::steady_clock::now();
}


void TimerHandler::restart_app_queue() {
  HT_ASSERT(m_app_queue_paused);
  auto now = std::chrono::steady_clock::now();
  int64_t pause_millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_pause_time).count();
  HT_INFOF("Restarting application queue (pause time = %lld millis)",
           (Lld)pause_millis);
  m_app_queue->start();
  m_app_queue_paused = false;
  m_current_interval = m_timer_interval;
  m_low_memory_mode = false;

  std::stringstream ss;
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  ss << seconds << "\tapp-queue-pause\t" << pause_millis;
  m_range_server->write_profile_data(ss.str());
}

bool TimerHandler::low_memory() {
  int64_t memory_used = Global::memory_tracker->balance();
  bool low_physical_memory = false;

  // ensure unused physical memory if it makes sense
  if (Global::memory_limit_ensure_unused_current &&
      memory_used - m_query_cache_memory > Global::memory_limit_ensure_unused_current) {

    // adjust current limit according to the actual memory situation
    int64_t free_memory = (int64_t)(System::mem_stat().free * MiB);
    if (Global::memory_limit_ensure_unused_current < Global::memory_limit_ensure_unused)
      Global::memory_limit_ensure_unused_current = std::min(free_memory, Global::memory_limit_ensure_unused);

    // low physical memory reached?
    low_physical_memory = free_memory < Global::memory_limit_ensure_unused_current;
    if (low_physical_memory)
      HT_INFOF("Low physical memory (free %.2fMB, limit %.2fMB)", free_memory / (double)MiB,
               Global::memory_limit_ensure_unused_current / (double)MiB);
  }

  return low_physical_memory || memory_used > Global::memory_limit;
}
