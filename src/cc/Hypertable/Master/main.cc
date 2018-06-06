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
 * Contains main function for Master server
 * This file contains the definition for the main function for the Master
 * server
 */

#include <Common/Compat.h>

#include <Hypertable/Master/BalancePlanAuthority.h>
#include <Hypertable/Master/ConnectionHandler.h>
#include <Hypertable/Master/Context.h>
#include <Hypertable/Master/LoadBalancer.h>
#include <Hypertable/Master/MetaLogDefinitionMaster.h>
#include <Hypertable/Master/Monitoring.h>
#include <Hypertable/Master/OperationInitialize.h>
#include <Hypertable/Master/OperationMoveRange.h>
#include <Hypertable/Master/OperationProcessor.h>
#include <Hypertable/Master/OperationRecover.h>
#include <Hypertable/Master/OperationRecoveryBlocker.h>
#include <Hypertable/Master/OperationSystemUpgrade.h>
#include <Hypertable/Master/OperationTimedBarrier.h>
#include <Hypertable/Master/OperationWaitForServers.h>
#include <Hypertable/Master/ReferenceManager.h>
#include <Hypertable/Master/ResponseManager.h>
#include <Hypertable/Master/SystemState.h>

#include <Hypertable/Lib/ClusterId.h>
#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/MetaLogReader.h>

#include <AsyncComm/Comm.h>

#include <Common/FailureInducer.h>
#include <Common/Init.h>
#include <Common/System.h>

#include <sstream>

extern "C" {
#include <poll.h>
}

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  struct AppPolicy : Config::Policy {
    static void init_options() {
      alias("port", "Hypertable.Master.Port");
      alias("workers", "Hypertable.Master.Workers");
      alias("reactors", "Hypertable.Master.Reactors");
    }
  };

  typedef Meta::list<GenericServerPolicy, FsClientPolicy,
                     HyperspaceClientPolicy, DefaultCommPolicy, AppPolicy> Policies;

  class HandlerFactory : public ConnectionHandlerFactory {
  public:
    HandlerFactory(ContextPtr &context) : m_context(context) {
      m_handler = make_shared<ConnectionHandler>(m_context);
    }

    virtual void get_instance(DispatchHandlerPtr &dhp) {
      dhp = m_handler;
    }

    void start_timer() {
      static_cast<ConnectionHandler*>(m_handler.get())->start_timer();
    }

  private:
    ContextPtr m_context;
    DispatchHandlerPtr m_handler;
  };

  struct ltrsc {
    bool operator()(const RangeServerConnectionPtr &rsc1, const RangeServerConnectionPtr &rsc2) const {
      return strcmp(rsc1->location().c_str(), rsc2->location().c_str()) < 0;
    }
  };


} // local namespace

/** @defgroup Master Master
 * @ingroup Hypertable
 * %Master server.
 * The Master module contains all of the definitions that make up the Master
 * server process which is responsible for handling meta operations such
 * the following
 *   - Creating, altering, and dropping tables
 *   - %CellStore garbage collection
 *   - Orchestration of %RangeServer failover
 *   - Load balancing
 * @{
 */


int main(int argc, char **argv) {
  int exit_status {};
  InetAddr listen_addr;
  ContextPtr context;

  // Register ourselves as the Comm-layer proxy master
  ReactorFactory::proxy_master = true;

  while (true) {

    try {
      init_with_policies<Policies>(argc, argv);
      uint16_t port = get_i16("port");
      listen_addr = InetAddr(INADDR_ANY, port);

      Hyperspace::SessionPtr hyperspace = make_shared<Hyperspace::Session>(Comm::instance(), properties);
      context = make_shared<Context>(properties, hyperspace);
      context->monitoring = make_shared<Monitoring>(context.get());
      context->op = std::make_unique<OperationProcessor>(context, get_i32("workers"));

      ConnectionHandlerFactoryPtr connection_handler_factory(new HandlerFactory(context));
      context->comm->listen(listen_addr, connection_handler_factory);

      auto func = [&context](bool status){context->set_startup_status(status);};
      if (!context->master_file->obtain_master_lock(context->toplevel_dir, func)){
        context->start_shutdown();
        context->op->join();
        break;
      }
    
      // Load (and possibly generate) the cluster ID
      ClusterId cluster_id(context->hyperspace, ClusterId::GENERATE_IF_NOT_FOUND);
      HT_INFOF("Cluster id is %llu", (Llu)ClusterId::get());

      context->mml_definition =
        make_shared<MetaLog::DefinitionMaster>(context, format("%s_%u", "master", port).c_str());
      
      if (has("induce-failure")) {
        if (FailureInducer::instance == 0)
          FailureInducer::instance = new FailureInducer();
        FailureInducer::instance->parse_option(get_str("induce-failure"));
      }

      /**
       * Read/load MML
       */
      std::vector<MetaLog::EntityPtr> entities;
      std::vector<OperationPtr> operations;
      std::map<String, OperationPtr> recovery_ops;
      MetaLog::ReaderPtr mml_reader;
      OperationPtr operation;
      RangeServerConnectionPtr rsc;
      String log_dir = context->toplevel_dir + "/servers/master/log/"
        + context->mml_definition->name();
      BalancePlanAuthority *bpa {};

      mml_reader = make_shared<MetaLog::Reader>(context->dfs, context->mml_definition,
                                                log_dir);
      mml_reader->get_entities(entities);

      // Uniq-ify the RangeServerConnection and BalancePlanAuthority objects
      {
        std::vector<MetaLog::EntityPtr> entities2;
        std::set<RangeServerConnectionPtr, ltrsc> rsc_set;

        entities2.reserve(entities.size());
        for (auto &entity : entities) {
          if (dynamic_cast<RangeServerConnection *>(entity.get())) {
            RangeServerConnectionPtr rsc {dynamic_pointer_cast<RangeServerConnection>(entity)};
            if (rsc_set.count(rsc) > 0)
              rsc_set.erase(rsc);
            rsc_set.insert(rsc);
          }
          else {
            if (dynamic_cast<BalancePlanAuthority *>(entity.get())) {
              bpa = dynamic_cast<BalancePlanAuthority *>(entity.get());
              context->set_balance_plan_authority(entity);
            }
            else if (dynamic_cast<SystemState *>(entity.get()))
              context->system_state = dynamic_pointer_cast<SystemState>(entity);
            else if (dynamic_cast<RecoveredServers *>(entity.get()))
              context->recovered_servers = dynamic_pointer_cast<RecoveredServers>(entity);
            entities2.push_back(entity);
          }
        }
        // Insert uniq'ed RangeServerConnections
        for (const auto &rsc : rsc_set)
          entities2.push_back(rsc);
        entities.swap(entities2);
      }

      if (!context->system_state)
        context->system_state = make_shared<SystemState>();

      if (!context->recovered_servers)
        context->recovered_servers = make_shared<RecoveredServers>();

      context->mml_writer =
        make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                     log_dir, entities);

      if (bpa) {
        bpa->set_mml_writer(context->mml_writer);
        std::stringstream sout;
        sout << "Loading BalancePlanAuthority: " << *bpa;
        HT_INFOF("%s", sout.str().c_str());
      }

      context->response_manager->set_mml_writer(context->mml_writer);

      // First do System Upgrade
      operation = make_shared<OperationSystemUpgrade>(context);
      context->op->add_operation(operation);
      context->op->wait_for_empty();

      // Then reconstruct state and start execution
      for (auto &entity : entities) {
        Operation *op = dynamic_cast<Operation *>(entity.get());
        if (op) {
          operation = dynamic_pointer_cast<Operation>(entity);
          if (op->get_remove_approval_mask() && !op->removal_approved())
            context->reference_manager->add(operation);

          if (dynamic_cast<OperationMoveRange *>(op))
            context->add_move_operation(operation);

          // master was interrupted in the middle of rangeserver failover
          if (dynamic_cast<OperationRecover *>(op)) {
            HT_INFO("Recovery was interrupted; continuing");
            OperationRecover *recover_op = dynamic_cast<OperationRecover *>(op);
            if (!recover_op->location().empty()) {
              operations.push_back(operation);
              recovery_ops[recover_op->location()] = operation;
            }
          }
          else
            operations.push_back(operation);
        }
        else if (dynamic_cast<RangeServerConnection *>(entity.get())) {
          rsc = dynamic_pointer_cast<RangeServerConnection>(entity);
          context->rsc_manager->add_server(rsc);
        }
      }
      context->balancer = new LoadBalancer(context);

      // For each RangeServerConnection that doesn't already have an
      // outstanding OperationRecover, create and add one
      for (auto &entity : entities) {
        if (dynamic_cast<RangeServerConnection *>(entity.get())) {
          rsc = dynamic_pointer_cast<RangeServerConnection>(entity);
          if (recovery_ops.find(rsc->location()) == recovery_ops.end())
            operations.push_back(make_shared<OperationRecover>(context, rsc, OperationRecover::RESTART));
        }
      }
      recovery_ops.clear();

      if (operations.empty()) {
        OperationPtr init_op = make_shared<OperationInitialize>(context);
        if (context->namemap->exists_mapping("/sys/METADATA", 0))
          init_op->set_state(OperationState::CREATE_RS_METRICS);
        operations.push_back(init_op);
      }
      else {
        if (!context->metadata_table)
          context->metadata_table = context->new_table(TableIdentifier::METADATA_NAME);

        if (!context->rs_metrics_table)
          context->rs_metrics_table = context->new_table("sys/RS_METRICS");
      }

      // Add PERPETUAL operations
      operation = make_shared<OperationWaitForServers>(context);
      operations.push_back(operation);
      context->recovery_barrier_op =
        make_shared<OperationTimedBarrier>(context, Dependency::RECOVERY, Dependency::RECOVERY_BLOCKER);
      operation = make_shared<OperationRecoveryBlocker>(context);
      operations.push_back(operation);

      context->op->add_operations(operations);

      context->master_file->write_master_address();

      /// Start timer loop
      static_cast<HandlerFactory*>(connection_handler_factory.get())->start_timer();

      if (!context->set_startup_status(false))
        context->start_shutdown();

      context->op->join();
    }
    catch (Exception &e) {
      HT_ERROR_OUT << e << HT_END;
      exit_status = 1;
    }
    break;
  }

  context->comm->close_socket(listen_addr);
  context.reset();

  if (has("pidfile"))
    FileUtils::unlink(get_str("pidfile"));

  Comm::destroy();

  return exit_status;
}

/** @}*/
