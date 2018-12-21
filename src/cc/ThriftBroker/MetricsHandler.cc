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

/// @file
/// Declarations for MetricsHandler.
/// This file contains declarations for MetricsHandler, a dispatch handler class
/// used to collect and publish %ThriftBroker metrics.

#include <Common/Compat.h>

#include "MetricsHandler.h"

#include <AsyncComm/Comm.h>

#include <Common/Error.h>
#include <Common/Logger.h>
#include <Common/Properties.h>

#include <mutex>

using namespace Hypertable;


MetricsHandler::MetricsHandler(PropertiesPtr &props, Cronolog *slow_query_log)
  : m_slow_query_log(slow_query_log) {
  m_ganglia_collector = std::make_shared<MetricsCollectorGanglia>("thriftbroker", props);
  m_collection_interval = props->get_i32("Hypertable.Monitoring.Interval");
  m_last_timestamp = Hypertable::get_ts64();
  m_comm = Comm::instance();
}

void MetricsHandler::start_collecting() {
  int error;
  if ((error = m_comm->set_timer(m_collection_interval, shared_from_this())) != Error::OK)
    HT_FATALF("Problem setting timer - %s", Error::get_text(error));
}

void MetricsHandler::stop_collecting() {
  m_comm->cancel_timer(shared_from_this());
}

void MetricsHandler::handle(Hypertable::EventPtr &event) {
  int error;

  if (event->type == Hypertable::Event::TIMER) {

    int64_t timestamp = Hypertable::get_ts64();

    m_metrics_process.collect(timestamp, m_ganglia_collector.get());

    int64_t elapsed_secs = (timestamp - m_last_timestamp) / 1000000000LL;

    m_ganglia_collector->update("requests", m_requests.rate(elapsed_secs));
    m_requests.reset();
    m_ganglia_collector->update("errors", m_errors.rate(elapsed_secs));
    m_errors.reset();
    m_ganglia_collector->update("connections", m_active_connections);

    try {
      m_ganglia_collector->publish();
    }
    catch (Exception &e) {
      HT_INFOF("Problem publishing Ganglia metrics - %s", e.what());
    }

    m_last_timestamp = timestamp;

    if (m_slow_query_log)
      m_slow_query_log->sync();

    if ((error = m_comm->set_timer(m_collection_interval, shared_from_this())) != Error::OK)
      HT_FATALF("Problem setting timer - %s", Error::get_text(error));

  }
  else
    HT_FATALF("Unrecognized event - %s", event->to_str().c_str());

}
