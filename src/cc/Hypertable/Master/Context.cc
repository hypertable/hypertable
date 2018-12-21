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
 * Definitions for Context.
 * This file contains definitions for Context, a class that provides execution
 * context for the Master.
 */

#include <Common/Compat.h>

#include "BalancePlanAuthority.h"
#include "Context.h"
#include "LoadBalancer.h"
#include "Operation.h"
#include "OperationProcessor.h"
#include "OperationRecover.h"
#include "OperationTimedBarrier.h"
#include "ReferenceManager.h"

#include <Hypertable/Lib/Master/Request/Parameters/PhantomCommitComplete.h>
#include <Hypertable/Lib/Master/Request/Parameters/PhantomPrepareComplete.h>
#include <Hypertable/Lib/Master/Request/Parameters/ReplayComplete.h>
#include <Hypertable/Lib/Master/Request/Parameters/ReplayStatus.h>

#include <FsBroker/Lib/Client.h>

#include <Common/FailureInducer.h>
#include <Common/SystemInfo.h>
#include <Common/md5.h>

#include <chrono>
#include <memory>

using namespace Hypertable;
using namespace Hypertable::Lib;
using namespace std;

Context::Context(PropertiesPtr &p, Hyperspace::SessionPtr hs) : props(p), hyperspace(hs), op(nullptr) {

  if (props->has("Hypertable.Cluster.Name")) {
    cluster_name = props->get_str("Hypertable.Cluster.Name");
    boost::trim_if(cluster_name, boost::is_any_of(" '\""));
  }

  disk_threshold = props->get_i32("Hypertable.Master.DiskThreshold.Percentage");

  toplevel_dir = props->get_str("Hypertable.Directory");
  boost::trim_if(toplevel_dir, boost::is_any_of("/"));
  toplevel_dir = String("/") + toplevel_dir;

  comm = Comm::instance();
  conn_manager = make_shared<ConnectionManager>(comm);
  reference_manager = std::make_unique<ReferenceManager>();
  response_manager = std::make_unique<ResponseManager>();
  response_manager_thread = std::make_unique<Thread>(*response_manager);
  dfs = std::make_shared<FsBroker::Lib::Client>(conn_manager, props);
  rsc_manager = make_shared<RangeServerConnectionManager>();
  metrics_handler = std::make_shared<MetricsHandler>(props);
  metrics_handler->start_collecting();

  int worker_count = props->get_i32("Hypertable.Client.Workers");
  app_queue = make_shared<ApplicationQueue>(worker_count);

  if (hyperspace) {
    namemap = make_shared<NameIdMapper>(hyperspace, toplevel_dir);
    master_file = std::make_unique<HyperspaceMasterFile>(props, hyperspace);
  }

  request_timeout = (time_t)(props->get_i32("Hypertable.Request.Timeout") / 1000);
  if (props->get_bool("Hypertable.Master.Locations.IncludeMasterHash")) {
    char hash[33];
    uint16_t port = props->get_i16("port");
    md5_string(format("%s:%u", System::net_info().host_name.c_str(), port).c_str(), hash);
    location_hash = String(hash).substr(0, 8);
  }
  max_allowable_skew = props->get_i32("Hypertable.RangeServer.ClockSkew.Max");
  monitoring_interval = props->get_i32("Hypertable.Monitoring.Interval");
  gc_interval = props->get_i32("Hypertable.Master.Gc.Interval");
  timer_interval = std::min(monitoring_interval, gc_interval);

  HT_ASSERT(monitoring_interval > 1000);
  HT_ASSERT(gc_interval > 1000);

  time_t now = time(0);
  next_monitoring_time = now + (monitoring_interval/1000) - 1;
  next_gc_time = now + (gc_interval/1000) - 1;
}


Context::~Context() {
  if (mml_writer)
    mml_writer->close();
  metrics_handler->stop_collecting();
  delete balancer;
}

bool Context::set_startup_status(bool status) {
  {
    lock_guard<std::mutex> lock(mutex);
    m_startup = status;
    if (status == false && m_shutdown)
      return false;
  }
  return true;
}

bool Context::startup_in_progress() {
  return m_startup;
}

void Context::start_shutdown() {
  {
    lock_guard<std::mutex> lock(mutex);
    if (m_shutdown)
      return;
    m_shutdown = true;
    if (m_startup)
      return;
  }
  metrics_handler->stop_collecting();
  master_file->shutdown();
  if (recovery_barrier_op)
    recovery_barrier_op->shutdown();
  op->wait_for_idle(chrono::seconds(15));
  op->shutdown();
  response_manager->shutdown();
  response_manager_thread->join();
}

bool Context::shutdown_in_progress() {
  return m_shutdown;
}




void Context::notification_hook(const String &subject, const String &message) {

  String cmd;

  if (!cluster_name.empty())
    cmd = format("%s/conf/notification-hook.sh '[Hypertable %s] %s' '%s'", 
                 System::install_dir.c_str(), cluster_name.c_str(),
                 subject.c_str(), message.c_str());
  else
    cmd = format("%s/conf/notification-hook.sh '[Hypertable] %s' '%s'", 
                 System::install_dir.c_str(), subject.c_str(),
                 message.c_str());

  int ret = ::system(cmd.c_str());
  HT_INFOF("notification-hook returned: %d", ret);
  if (ret != 0) {
    HT_WARNF("shell script conf/notification-hook.sh ('%s') returned "
            "error %d", cmd.c_str(), ret);
  }
}

void Context::set_balance_plan_authority(MetaLog::EntityPtr bpa) {
  lock_guard<std::mutex> lock(mutex);
  HT_ASSERT(dynamic_cast<BalancePlanAuthority *>(bpa.get()));
  m_balance_plan_authority = bpa;
}

BalancePlanAuthority *Context::get_balance_plan_authority() {
  lock_guard<std::mutex> lock(mutex);
  if (!m_balance_plan_authority)
    m_balance_plan_authority = make_shared<BalancePlanAuthority>(shared_from_this(), mml_writer);
  return static_cast<BalancePlanAuthority *>(m_balance_plan_authority.get());
}

void Context::get_balance_plan_authority(MetaLog::EntityPtr &entity) {
  lock_guard<std::mutex> lock(mutex);
  if (!m_balance_plan_authority)
    m_balance_plan_authority = make_shared<BalancePlanAuthority>(shared_from_this(), mml_writer);
  entity = m_balance_plan_authority;
}

TablePtr Context::new_table(const std::string &name) {
  if (!range_locator)
    range_locator = make_shared<RangeLocator>(props, conn_manager, hyperspace, request_timeout * 1000);
  ApplicationQueueInterfacePtr aq = app_queue;
  return make_shared<Table>(props, range_locator, conn_manager,
                            hyperspace, aq, namemap, name);
}

void Context::replay_status(EventPtr &event) {
  Lib::Master::Request::Parameters::ReplayStatus params;
  const uint8_t *decode_ptr = event->payload;
  size_t decode_remain = event->payload_len;

  params.decode(&decode_ptr, &decode_remain);

  String proxy;
  if (event->proxy == 0) {
    RangeServerConnectionPtr rsc;
    if (!rsc_manager->find_server_by_local_addr(event->addr, rsc)) {
      HT_WARNF("Unable to determine proxy for replay_status(id=%lld, %s, "
               "plan_generation=%d) from %s", (Lld)params.op_id(),
               params.location().c_str(), params.plan_generation(),
               event->addr.format().c_str());
      return;
    }
    proxy = rsc->location();
  }
  else
    proxy = event->proxy;

  HT_INFOF("replay_status(id=%lld, %s, plan_generation=%d) from %s",
           (Lld)params.op_id(), params.location().c_str(),
           params.plan_generation(), proxy.c_str());

  RecoveryStepFuturePtr future = m_recovery_state.get_replay_future(params.op_id());

  if (future)
    future->status(proxy, params.plan_generation());
  else
    HT_WARN_OUT << "No Recovery replay step future found for operation=" << params.op_id() << HT_END;

}

void Context::replay_complete(EventPtr &event) {
  Lib::Master::Request::Parameters::ReplayComplete params;
  const uint8_t *decode_ptr = event->payload;
  size_t decode_remain = event->payload_len;

  params.decode(&decode_ptr, &decode_remain);

  String proxy;
  if (event->proxy == 0) {
    RangeServerConnectionPtr rsc;
    if (!rsc_manager->find_server_by_local_addr(event->addr, rsc)) {
      HT_WARNF("Unable to determine proxy for replay_complete(id=%lld, %s, "
               "plan_generation=%d) from %s", (Lld)params.op_id(),
               params.location().c_str(), params.plan_generation(),
               event->addr.format().c_str());
      return;
    }
    proxy = rsc->location();
  }
  else
    proxy = event->proxy;

  HT_INFOF("from %s replay_complete(id=%lld, %s, plan_generation=%d) = %s",
           proxy.c_str(), (Lld)params.op_id(), params.location().c_str(),
           params.plan_generation(), Error::get_text(params.error()));

  RecoveryStepFuturePtr future
    = m_recovery_state.get_replay_future(params.op_id());

  if (future) {
    if (params.error() == Error::OK)
      future->success(proxy, params.plan_generation());
    else
      future->failure(proxy, params.plan_generation(), params.error(),
                      params.message());
  }
  else
    HT_WARN_OUT << "No Recovery replay step future found for operation="
                << params.op_id() << HT_END;

}

void Context::prepare_complete(EventPtr &event) {
  Lib::Master::Request::Parameters::PhantomPrepareComplete params;
  const uint8_t *decode_ptr = event->payload;
  size_t decode_remain = event->payload_len;

  params.decode(&decode_ptr, &decode_remain);

  HT_INFOF("prepare_complete(id=%lld, %s, plan_generation=%d) = %s",
           (Lld)params.op_id(), params.location().c_str(),
           params.plan_generation(), Error::get_text(params.error()));

  RecoveryStepFuturePtr future
    = m_recovery_state.get_prepare_future(params.op_id());

  String proxy;
  if (event->proxy == 0) {
    RangeServerConnectionPtr rsc;
    if (!rsc_manager->find_server_by_local_addr(event->addr, rsc)) {
      HT_WARNF("Unable to determine proxy for prepare_complete(id=%lld, %s, "
               "plan_generation=%d) from %s", (Lld)params.op_id(),
               params.location().c_str(), params.plan_generation(),
               event->addr.format().c_str());
      return;
    }
    proxy = rsc->location();
  }
  else
    proxy = event->proxy;

  if (future) {
    if (params.error() == Error::OK)
      future->success(proxy, params.plan_generation());
    else
      future->failure(proxy, params.plan_generation(), params.error(),
                      params.message());
  }
  else
    HT_WARN_OUT << "No Recovery prepare step future found for operation="
                << params.op_id() << HT_END;
}

void Context::commit_complete(EventPtr &event) {
  Lib::Master::Request::Parameters::PhantomPrepareComplete params;
  const uint8_t *decode_ptr = event->payload;
  size_t decode_remain = event->payload_len;

  params.decode(&decode_ptr, &decode_remain);

  HT_INFOF("commit_complete(id=%lld, %s, plan_generation=%d) = %s",
           (Lld)params.op_id(), params.location().c_str(),
           params.plan_generation(), Error::get_text(params.error()));

  RecoveryStepFuturePtr future
    = m_recovery_state.get_commit_future(params.op_id());

  String proxy;
  if (event->proxy == 0) {
    RangeServerConnectionPtr rsc;
    if (!rsc_manager->find_server_by_local_addr(event->addr, rsc)) {
      HT_WARNF("Unable to determine proxy for commit_complete(id=%lld, %s, "
               "plan_generation=%d) from %s", (Lld)params.op_id(),
               params.location().c_str(), params.plan_generation(),
               event->addr.format().c_str());
      return;
    }
    proxy = rsc->location();
  }
  else
    proxy = event->proxy;

  if (future) {
    if (params.error() == Error::OK)
      future->success(proxy, params.plan_generation());
    else
      future->failure(proxy, params.plan_generation(), params.error(),
                      params.message());
  }
  else
    HT_WARN_OUT << "No Recovery commit step future found for operation="
                << params.op_id() << HT_END;
}

bool Context::add_move_operation(OperationPtr operation) {
  lock_guard<std::mutex> lock(m_outstanding_move_ops_mutex);
  auto iter = m_outstanding_move_ops.find(operation->hash_code());
  if (iter != m_outstanding_move_ops.end())
    return false;
  m_outstanding_move_ops[operation->hash_code()] = operation->id();
  return true;
}

void Context::remove_move_operation(OperationPtr operation) {
  lock_guard<std::mutex> lock(m_outstanding_move_ops_mutex);
  auto iter = m_outstanding_move_ops.find(operation->hash_code());
  if (iter == m_outstanding_move_ops.end())
    return;
  m_outstanding_move_ops.erase(iter);
}

OperationPtr Context::get_move_operation(int64_t hash_code) {
  lock_guard<std::mutex> lock(m_outstanding_move_ops_mutex);
  auto iter = m_outstanding_move_ops.find(hash_code);
  if (iter != m_outstanding_move_ops.end()) {
    OperationPtr operation = reference_manager->get(iter->second);
    HT_ASSERT(operation);
    return operation;
  }
  return OperationPtr();
}

void Context::add_available_server(const String &location) {
  lock_guard<std::mutex> lock(mutex);
  available_servers.insert(location);
}

void Context::remove_available_server(const String &location) {
  lock_guard<std::mutex> lock(mutex);
  available_servers.erase(location);
}

size_t Context::available_server_count() {
  lock_guard<std::mutex> lock(mutex);
  return available_servers.size();
}

void Context::get_available_servers(StringSet &servers) {
  lock_guard<std::mutex> lock(mutex);
  servers = available_servers;
}


bool Context::can_accept_ranges(const RangeServerStatistics &stats)
{

  // system info was not yet initialized; assume that the disks are available
  if (!stats.stats) {
    HT_WARNF("RangeServer %s: no disk usage statistics available",
            stats.location.c_str());
    return true;
  }

  // accept new ranges if there's at least one disk below the threshold
  double numerator=0.0, denominator=0.0;
  for (const auto &fs : stats.stats->system.fs_stat) {
    numerator += fs.total - fs.avail;
    denominator += fs.total;
  }

  if (denominator == 0.0 ||
      (int32_t)((numerator/denominator)*100.00) < disk_threshold)
    return true;

  HT_WARNF("RangeServer %s: disk use %d%% exceeds threshold; will not assign "
           "ranges", stats.location.c_str(),
           (int32_t)((numerator/denominator)*100.00));

  return false;
}


void
Context::RecoveryState::install_replay_future(int64_t id,
                                              RecoveryStepFuturePtr &future) {
  lock_guard<std::mutex> lock(m_mutex);
  m_replay_map[id] = future;
}

RecoveryStepFuturePtr
Context::RecoveryState::get_replay_future(int64_t id) {
  lock_guard<std::mutex> lock(m_mutex);
  RecoveryStepFuturePtr future;
  if (m_replay_map.find(id) != m_replay_map.end())
    future = m_replay_map[id];
  return future;
}

void
Context::RecoveryState::erase_replay_future(int64_t id) {
  lock_guard<std::mutex> lock(m_mutex);
  m_replay_map.erase(id);
}


void
Context::RecoveryState::install_prepare_future(int64_t id,
                                               RecoveryStepFuturePtr &future) {
  lock_guard<std::mutex> lock(m_mutex);
  m_prepare_map[id] = future;
}

RecoveryStepFuturePtr
Context::RecoveryState::get_prepare_future(int64_t id) {
  lock_guard<std::mutex> lock(m_mutex);
  RecoveryStepFuturePtr future;
  if (m_prepare_map.find(id) != m_prepare_map.end())
    future = m_prepare_map[id];
  return future;
}

void
Context::RecoveryState::erase_prepare_future(int64_t id) {
  lock_guard<std::mutex> lock(m_mutex);
  m_prepare_map.erase(id);
}


void
Context::RecoveryState::install_commit_future(int64_t id,
                                               RecoveryStepFuturePtr &future) {
  lock_guard<std::mutex> lock(m_mutex);
  m_commit_map[id] = future;
}

RecoveryStepFuturePtr
Context::RecoveryState::get_commit_future(int64_t id) {
  lock_guard<std::mutex> lock(m_mutex);
  RecoveryStepFuturePtr future;
  if (m_commit_map.find(id) != m_commit_map.end())
    future = m_commit_map[id];
  return future;
}

void
Context::RecoveryState::erase_commit_future(int64_t id) {
  lock_guard<std::mutex> lock(m_mutex);
  m_commit_map.erase(id);
}

