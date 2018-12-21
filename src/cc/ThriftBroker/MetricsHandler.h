/* -*- c++ -*-
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

#ifndef ThriftBroker_MetricsHandler_h
#define ThriftBroker_MetricsHandler_h

#include <AsyncComm/Comm.h>
#include <AsyncComm/DispatchHandler.h>

#include <Common/Cronolog.h>
#include <Common/MetricsCollectorGanglia.h>
#include <Common/MetricsProcess.h>
#include <Common/Properties.h>
#include <Common/metrics>

#include <atomic>
#include <memory>
#include <mutex>

namespace Hypertable {


  /// @addtogroup ThriftBroker
  /// @{

  /// Collects and publishes %ThriftBroker metrics.
  /// This class acts as the timer dispatch handler for periodic metrics
  /// collection for the ThriftBroker.
  class MetricsHandler : public DispatchHandler {
  public:

    /// Constructor.
    /// Initializes #m_collection_interval to the property
    /// <code>Hypertable.Monitoring.Interval</code> and allocates a Ganglia
    /// collector object, initializing it with "thriftbroker" and
    /// <code>Hypertable.Metrics.Ganglia.Port</code>.  Lastly, calls
    /// Comm::set_timer() to register a timer for
    /// #m_collection_interval milliseconds in the future and passes
    /// <code>this</code> as the timer handler.
    /// @param props %Properties object
    /// @param slow_query_log Slow query log
    MetricsHandler(PropertiesPtr &props, Cronolog *slow_query_log);

    /// Destructor.
    /// Cancels the timer.
    virtual ~MetricsHandler() {};

    /// Starts metrics collection.
    void start_collecting();

    /// Stops metrics collection.
    void stop_collecting();

    /// Collects and publishes metrics.
    /// This method computes and updates the requests/s, errors, connections,
    /// and general process metrics and publishes them via #m_ganglia_collector.
    /// After metrics have been collected, if #m_slow_query_log is not null,
    /// then it is synced and the timer is re-registered for
    /// #m_collection_interval milliseconds in the future.
    /// @param event %Comm layer timer event
    virtual void handle(EventPtr &event);

    /// Increments request count.
    /// Increments request count which is used in computing requests/s.
    void request_increment() {
      m_requests.current++;
    }

    /// Increments error count.
    /// Increments error count which is used in computing errors/s.
    void error_increment() {
      m_errors.current++;
    }

    /// Increments connection count.
    /// Increments active connection count.
    void connection_increment() {
      m_active_connections++;
    }

    /// Decrements connection count.
    /// Decrements active connection count.
    void connection_decrement() {
      m_active_connections--;
    }

  private:

    /// Comm layer
    Comm *m_comm {};

    /// Ganglia metrics collector
    MetricsCollectorGangliaPtr m_ganglia_collector;

    /// General process metrics tracker
    MetricsProcess m_metrics_process;

    /// Slow query log
    Cronolog *m_slow_query_log {};

    /// %Timestamp of last metrics collection
    int64_t m_last_timestamp;

    /// %Metrics collection interval
    int32_t m_collection_interval {};

    /// %ThriftBroker requests
    interval_metric<int64_t> m_requests {};

    /// %ThriftBroker errors
    interval_metric<int64_t> m_errors {};

    /// Active %ThriftBroker connections
    atomic<int32_t> m_active_connections {0};
  };

  /// Shared smart pointer to MetricsHandler
  typedef std::shared_ptr<MetricsHandler> MetricsHandlerPtr;

  /// @}
}

#endif // ThriftBroker_MetricsHandler_h
