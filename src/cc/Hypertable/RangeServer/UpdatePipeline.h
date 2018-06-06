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
/// Declarations for UpdatePipeline.
/// This file contains type declarations for UpdatePipeline, a three-staged,
/// multithreaded update pipeline.

#ifndef Hypertable_RangeServer_UpdatePipeline_h
#define Hypertable_RangeServer_UpdatePipeline_h

#include <Hypertable/RangeServer/Context.h>
#include <Hypertable/RangeServer/QueryCache.h>
#include <Hypertable/RangeServer/TimerHandler.h>
#include <Hypertable/RangeServer/UpdateContext.h>

#include <Hypertable/Lib/KeySpec.h>

#include <Common/ByteString.h>
#include <Common/DynamicBuffer.h>
#include <Common/Filesystem.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace Hypertable {

  /// @addtogroup RangeServer
  /// @{

  /// Three-staged, multithreaded update pipeline.
  class UpdatePipeline {
  public:

    /// Constructor.
    /// Initializes the pipeline as follows:
    ///   - Sets #m_update_coalesce_limit to the value of the
    ///     <code>Hypertable.RangeServer.UpdateCoalesceLimit</code> property.
    ///   - Sets #m_maintenance_pause_interval to the value of the
    ///     <code>Hypertable.RangeServer.Testing.MaintenanceNeeded.PauseInterval</code>
    ///     property.
    ///   - Sets #m_update_delay to the value of the
    ///     <code>Hypertable.RangeServer.UpdateDelay</code> property.
    ///   - Sets #m_max_clock_skew to the value of the
    ///     <code>Hypertable.RangeServer.ClockSkew.Max</code> property.
    ///   - Creates and starts the three pipeline threads using
    ///     qualify_and_transform(), commit(), and add_and_respond() as the
    ///     thread functions, respectively.
    /// @param context %Range server context
    /// @param query_cache Query cache
    /// @param timer_handler Timer handler
    UpdatePipeline(ContextPtr &context, QueryCachePtr &query_cache,
                   TimerHandlerPtr &timer_handler, CommitLogPtr &log,
                   Filesystem::Flags flags);

    /// Adds updates to pipeline
    /// Adds <code>uc</code> to #m_qualify_queue and signals #m_qualify_queue_cond.
    /// @param uc Update context
    void add(UpdateContext *uc);

    /// Shuts down the pipeline
    /// Sets #m_shutdown to <i>true</i>, signals the three pipeline condition
    /// variables, and performs a join on each pipeline thread.
    void shutdown();

  private:

    /// Thread function for stage 1 of update pipeline.
    /// For each UpdateContext object on the input queue #m_qualify_queue, this
    /// function does the following:
    ///   - Extracts the updates that are destined for this range server
    ///   - Creates a SendBackRec for the key/value pairs not destined for
    ///     this range server.
    ///   - Transforms each key with a call to transform_key().
    ///   - Buffers the key/value pairs for downstream processing.
    ///   - Adds the UpdateContext objects to #m_commit_queue and signals
    ///     #m_commit_queue_cond.
    void qualify_and_transform();

    /// Thread function for stage 2 of update pipeline.
    /// For each UpdateContext object on the input queue #m_commit_queue, this
    /// function does the following:
    ///   - Writes the key/value pairs that were buffered in the previous stage
    ///     to the appropriate
    ///     commit log (or transfer log) <b>without</b> calling sync().
    ///   - Once either #m_update_coalesce_limit amount of updates has been
    ///     collected or when #m_qualify_queue becomes empty, sync() is called
    ///     on the commit (or transfer) log.
    ///   - Adds the UpdateContext objects to #m_response_queue and signals
    ///     #m_response_queue_cond.
    void commit();

    /// Thread function for stage 3 of update pipeline.
    /// For each UpdateContext object on the input queue #m_response_queue, this
    /// function does the following:
    ///   - Adds the key/value pairs that were commited in the previous state to
    ///     their appropriate ranges
    ///   - Sends back a response to the originating requests
    void add_and_respond();

    void transform_key(ByteString &bskey, DynamicBuffer *dest_bufp,
                       int64_t revision, int64_t *revisionp,
                       bool timeorder_desc);

    /// %Range server context
    std::shared_ptr<RangeServerContext> m_context;

    /// Pointer to query cache
    QueryCachePtr m_query_cache;

    /// Pointer to timer handler
    TimerHandlerPtr m_timer_handler;

    /// Pointer to commit log
    CommitLogPtr m_log {};

    /// %Mutex protecting stage 1 input queue
    std::mutex m_qualify_queue_mutex;

    /// Condition variable signaling addition to stage 1 input queue
    std::condition_variable m_qualify_queue_cond;

    /// Stage 1 input queue
    std::list<UpdateContext *> m_qualify_queue;

    /// %Mutex protecting stage 2 input queue
    std::mutex m_commit_queue_mutex;

    /// Condition variable signaling addition to stage 2 input queue
    std::condition_variable m_commit_queue_cond;

    /// Count of objects in stage 2 input queue
    int32_t m_commit_queue_count {};

    /// Stage 2 input queue
    std::list<UpdateContext *> m_commit_queue;

    /// %Mutex protecting stage 3 input queue
    std::mutex m_response_queue_mutex;

    /// Condition variable signaling addition to stage 3 input queue
    std::condition_variable m_response_queue_cond;

    /// Stage 3 input queue
    std::list<UpdateContext *> m_response_queue;

    /// Update pipeline threads
    std::vector<std::thread> m_threads;

    /// Last (largest) assigned revision number
    int64_t m_last_revision {TIMESTAMP_MIN};

    /// Commit log coalesce limit
    uint64_t m_update_coalesce_limit {};

    /// Millisecond pause time at the end of the pipeline (TESTING)
    int32_t m_maintenance_pause_interval {};

    /// Update delay at start of pipeline (TESTING)
    uint32_t m_update_delay {};

    /// Maximum allowable clock skew
    int32_t m_max_clock_skew {};

    /// Commit log flush flag (NONE, FLUSH, or SYNC)
    Filesystem::Flags m_flags {};

    /// Flag indicating if pipeline is being shut down
    bool m_shutdown {};
  };

  /// Smart pointer to UpdatePipeline
  typedef std::shared_ptr<UpdatePipeline> UpdatePipelinePtr;

  /// @}

}

#endif // Hypertable_RangeServer_UpdatePipeline_h
