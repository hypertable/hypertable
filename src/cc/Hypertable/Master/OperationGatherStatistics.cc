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
 * Definitions for OperationGatherStatistics.
 * This file contains definitions for OperationGatherStatistics, an operation
 * for fetching statistics from all RangeServers and processing.
 */

#include <Common/Compat.h>

#include "DispatchHandlerOperationGetStatistics.h"
#include "OperationGatherStatistics.h"
#include "OperationSetState.h"
#include "OperationProcessor.h"
#include "RangeServerStatistics.h"
#include "LoadBalancer.h"

#include <Common/Path.h>
#include <Common/StringExt.h>

#include <cstdlib>

using namespace Hypertable;
using namespace std;

OperationGatherStatistics::OperationGatherStatistics(ContextPtr &context)
  : OperationEphemeral(context, MetaLog::EntityType::OPERATION_GATHER_STATISTICS) {
  m_dependencies.insert(Dependency::INIT);
  m_dependencies.insert(Dependency::METADATA);
  m_dependencies.insert(Dependency::SYSTEM);
}

void OperationGatherStatistics::execute() {
  StringSet locations;
  std::vector<RangeServerConnectionPtr> servers;
  std::vector<RangeServerStatistics> results;
  int32_t state = get_state();
  DispatchHandlerOperationGetStatistics dispatch_handler(m_context);
  Path data_dir;
  String monitoring_dir, graphviz_str;
  String filename, filename_tmp;
  String dot_cmd;

  HT_INFOF("Entering GatherStatistics-%lld state=%s",
           (Lld)header.id, OperationState::get_text(state));

  switch (state) {

  case OperationState::INITIAL:
    m_context->rsc_manager->get_servers(servers);
    if (servers.empty()) {
      complete_ok();
      break;
    }
    results.resize(servers.size());
    for (size_t i=0; i<servers.size(); i++) {
      results[i].addr = servers[i]->local_addr();
      results[i].location = servers[i]->location();
      locations.insert(results[i].location);
    }
    dispatch_handler.initialize(results);
    dispatch_handler.DispatchHandlerOperation::start(locations);

    // Write MOP "graphviz" files
    m_context->op->graphviz_output(graphviz_str);
    data_dir = Path(m_context->props->get_str("Hypertable.DataDirectory"));
    monitoring_dir = (data_dir /= "/run/monitoring").string();
    filename = monitoring_dir + "/mop.dot";
    filename_tmp = monitoring_dir + "/mop.tmp.dot";
    if (FileUtils::write(filename_tmp, graphviz_str) != -1)
      FileUtils::rename(filename_tmp, filename);
    dot_cmd = format("dot -Tsvg -Gcharset=latin1 -o%s/mop.tmp.svg %s/mop.dot",
                     monitoring_dir.c_str(), monitoring_dir.c_str());
    if (system(dot_cmd.c_str()) != -1) {
      filename = monitoring_dir + "/mop.svg";
      filename_tmp = monitoring_dir + "/mop.tmp.svg";
      FileUtils::rename(filename_tmp, filename);
    }

    dispatch_handler.wait_for_completion();

    {
      double numerator, denominator;
      double numerator_total = 0.0, denominator_total = 0.0;
      std::vector<RangeServerState> rs_states;
      RangeServerState rs_state;
      for (auto &rs_stats : results) {
        if (rs_stats.fetch_error == Error::OK) {
          numerator = denominator = 0.0;
          for (size_t i=0; i<rs_stats.stats->system.fs_stat.size(); i++) {
            numerator += rs_stats.stats->system.fs_stat[i].total 
              - rs_stats.stats->system.fs_stat[i].avail;
            denominator += rs_stats.stats->system.fs_stat[i].total;
          }
          rs_state.location = rs_stats.location;
          if (denominator > 0.0)
            rs_state.disk_usage = (numerator / denominator) * 100.0;
          else
            rs_state.disk_usage = 0.0;
          /*
          HT_INFOF("%s disk_usage (%f/%f) = %0.2f", rs_state.location.c_str(),
                   numerator, denominator, rs_state.disk_usage);
          */
          rs_states.push_back(rs_state);
          numerator_total += numerator;
          denominator_total += denominator;
        }
      }
      m_context->rsc_manager->set_range_server_state(rs_states);

      HT_INFOF("Aggregate disk usage = %0.2f",
               denominator_total == 0.0 ? 0.0 : ((numerator_total / denominator_total)*100.0));

      int32_t aggregate_disk_usage = 0;
      if (denominator_total >= 0.0)
        aggregate_disk_usage = (int32_t)((numerator_total / denominator_total)*100.0);
      String message = format("because aggreggate disk usage is %d%%",
                              aggregate_disk_usage);
      bool readonly_mode = aggregate_disk_usage >= m_context->disk_threshold;

      if (m_context->system_state->auto_set(SystemVariable::READONLY,
                                            readonly_mode, message))
        m_context->op->add_operation(make_shared<OperationSetState>(m_context));
      else {
        // This isn't necessary in above block because OperationSetState does it
        std::vector<NotificationMessage> notifications;
        if (m_context->system_state->get_notifications(notifications)) {
          for (auto &msg : notifications)
            m_context->notification_hook(msg.subject, msg.body);
        }
      }
    }

    m_context->monitoring->add(results);
    m_context->balancer->transfer_monitoring_data(results);
    complete_ok();
    break;

  default:
    HT_FATALF("Unrecognized state %d", state);
  }

  HT_INFOF("Leaving GatherStatistics-%lld", (Lld)header.id);
}

const String OperationGatherStatistics::name() {
  return "OperationGatherStatistics";
}

const String OperationGatherStatistics::label() {
  return "GatherStatistics";
}

