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
#include "Common/Config.h"
#include "Common/Error.h"
#include "Common/FailureInducer.h"
#include "Common/StringExt.h"
#include "Common/Serialization.h"

#include "AsyncComm/ResponseCallback.h"

#include "Hypertable/Lib/MasterProtocol.h"

#include "ConnectionHandler.h"

#include "OperationAlterTable.h"
#include "OperationBalance.h"
#include "OperationCollectGarbage.h"
#include "OperationCreateNamespace.h"
#include "OperationCreateTable.h"
#include "OperationDropNamespace.h"
#include "OperationDropTable.h"
#include "OperationGatherStatistics.h"
#include "OperationProcessor.h"
#include "OperationMoveRange.h"
#include "OperationRecoverServer.h"
#include "OperationRegisterServer.h"
#include "OperationRelinquishAcknowledge.h"
#include "OperationRenameTable.h"
#include "OperationStatus.h"
#include "OperationLoadBalancer.h"
#include "OperationStop.h"
#include "RangeServerConnection.h"
#include "ReferenceManager.h"


using namespace Hypertable;
using namespace Serialization;
using namespace Error;


/**
 *
 */
ConnectionHandler::ConnectionHandler(ContextPtr &context) : m_context(context), m_shutdown(false) {
  int error;
  if ((error = m_context->comm->set_timer(context->timer_interval, this)) != Error::OK)
    HT_FATALF("Problem setting timer - %s", Error::get_text(error));
}


/**
 *
 */
void ConnectionHandler::handle(EventPtr &event) {
  OperationPtr operation;
  boost::xtime expire_time;

  if (m_shutdown &&
      event->header.command != MasterProtocol::COMMAND_SHUTDOWN &&
      event->header.command != MasterProtocol::COMMAND_STATUS) {
    ResponseCallback cb(m_context->comm, event);
    cb.error(Error::SERVER_SHUTTING_DOWN, "");
    return;
  }

  if (event->type == Event::MESSAGE) {

    //event->display()

    try {
      // sanity check command code
      if (event->header.command < 0
          || event->header.command >= MasterProtocol::COMMAND_MAX)
        HT_THROWF(PROTOCOL_ERROR, "Invalid command (%llu)",
                  (Llu)event->header.command);

      switch (event->header.command) {
      case MasterProtocol::COMMAND_CREATE_TABLE:
        operation = new OperationCreateTable(m_context, event);
        break;
      case MasterProtocol::COMMAND_DROP_TABLE:
        operation = new OperationDropTable(m_context, event);
        break;
      case MasterProtocol::COMMAND_ALTER_TABLE:
        operation = new OperationAlterTable(m_context, event);
        break;
      case MasterProtocol::COMMAND_RENAME_TABLE:
        operation = new OperationRenameTable(m_context, event);
        break;
      case MasterProtocol::COMMAND_STATUS:
        operation = new OperationStatus(m_context, event);
        break;
      case MasterProtocol::COMMAND_REGISTER_SERVER:
        operation = new OperationRegisterServer(m_context, event);
        m_context->op->add_operation(operation);
        return;
      case MasterProtocol::COMMAND_MOVE_RANGE:
        operation = new OperationMoveRange(m_context, event);
	if (!m_context->reference_manager->add(operation)) {
          HT_INFOF("Skipping %s because already in progress", operation->label().c_str());
          send_error_response(event, Error::MASTER_OPERATION_IN_PROGRESS, "");
          return;
        }
        m_context->op->add_operation(operation);
        return;
      case MasterProtocol::COMMAND_RELINQUISH_ACKNOWLEDGE:
        operation = new OperationRelinquishAcknowledge(m_context, event);
        break;
      case MasterProtocol::COMMAND_BALANCE:
        operation = new OperationLoadBalancer(m_context, event);
        break;
      case MasterProtocol::COMMAND_STOP:
        operation = new OperationStop(m_context, event);
        break;
      case MasterProtocol::COMMAND_SHUTDOWN:
        HT_INFO("Received shutdown command");
        m_shutdown = true;
        m_context->op->shutdown();
        boost::xtime_get(&expire_time, boost::TIME_UTC);
        expire_time.sec += 15;
        m_context->op->timed_wait_for_idle(expire_time);
        m_context->op->shutdown();
        m_context->mml_writer->close();
        _exit(0);
      case MasterProtocol::COMMAND_CREATE_NAMESPACE:
        operation = new OperationCreateNamespace(m_context, event);
        break;
      case MasterProtocol::COMMAND_DROP_NAMESPACE:
        operation = new OperationDropNamespace(m_context, event);
        break;

      case MasterProtocol::COMMAND_FETCH_RESULT:
        m_context->response_manager->add_delivery_info(event);
        return;

      default:
        HT_THROWF(PROTOCOL_ERROR, "Unimplemented command (%llu)",
                  (Llu)event->header.command);
      }
      if (operation) {
        HT_INFOF("About to load %u", (unsigned)event->header.command);
        HT_MAYBE_FAIL_X("connection-handler-before-id-response",
                        event->header.command != MasterProtocol::COMMAND_STATUS &&
                        event->header.command != MasterProtocol::COMMAND_RELINQUISH_ACKNOWLEDGE);
        if (send_id_response(event, operation) != Error::OK)
          return;
        m_context->op->add_operation(operation);
      }
      else {
        ResponseCallback cb(m_context->comm, event);
        cb.error(Error::PROTOCOL_ERROR,
                 format("Unimplemented command (%llu)", (Llu)event->header.command));
      }
    }
    catch (Exception &e) {
      if (e.code() == Error::MASTER_OPERATION_IN_PROGRESS)
        HT_WARNF("%s", e.what());
      else
        HT_ERROR_OUT << e << HT_END;
      if (operation) {
        operation->complete_error_no_log(e.code(), e.what());
        m_context->response_manager->add_operation(operation);
      }
    }
  }
  else if (event->type == Event::DISCONNECT) {
    RangeServerConnectionPtr rsc;
    if (m_context->find_server_by_local_addr(event->addr, rsc)) {
      if (m_context->disconnect_server(rsc)) {
        OperationPtr operation = new OperationRecoverServer(m_context, rsc);
        m_context->op->add_operation(operation);
      }
    }
    HT_INFOF("%s", event->to_str().c_str());
  }
  else if (event->type == Hypertable::Event::TIMER) {
    OperationPtr operation;
    int error;
    time_t now = time(0);

    try {
      if (m_context->next_monitoring_time <= now) {
        operation = new OperationGatherStatistics(m_context);
        m_context->op->add_operation(operation);
        m_context->next_monitoring_time = now + (m_context->monitoring_interval/1000) - 1;
      }

      if (m_context->next_gc_time <= now) {
        operation = new OperationCollectGarbage(m_context);
        m_context->op->add_operation(operation);
        m_context->next_gc_time = now + (m_context->gc_interval/1000) - 1;
      }
      operation = new OperationLoadBalancer(m_context);
      m_context->op->add_operation(operation);
    }
    catch (Exception &e) {
      if (e.code() == Error::MASTER_OPERATION_IN_PROGRESS)
        HT_WARNF("%s", e.what());
      else
        HT_ERROR_OUT << e << HT_END;
    }

    if ((error = m_context->comm->set_timer(m_context->timer_interval, this)) != Error::OK)
      HT_FATALF("Problem setting timer - %s", Error::get_text(error));

  }
  else {
    HT_INFOF("%s", event->to_str().c_str());
  }

}

int32_t ConnectionHandler::send_id_response(EventPtr &event, OperationPtr &operation) {
  int error = Error::OK;
  CommHeader header;
  header.initialize_from_request_header(event->header);
  CommBufPtr cbp(new CommBuf(header, 12));
  cbp->append_i32(Error::OK);
  cbp->append_i64(operation->id());
  error = m_context->comm->send_response(event->addr, cbp);
  if (error != Error::OK)
    HT_ERRORF("Problem sending ID response back for %s operation (id=%lld) - %s",
              operation->label().c_str(), (Lld)operation->id(), Error::get_text(error));
  return error;
}


int32_t ConnectionHandler::send_error_response(EventPtr &event, int32_t error, const String &msg) {
  CommHeader header;
  header.initialize_from_request_header(event->header);
  CommBufPtr cbp(new CommBuf(header, 4 + Serialization::encoded_length_vstr(msg)));
  cbp->append_i32(error);
  cbp->append_vstr(msg);
  int ret = m_context->comm->send_response(event->addr, cbp);
  if (ret != Error::OK)
    HT_ERRORF("Problem sending error response back to %s - %s",
              event->addr.format().c_str(), Error::get_text(error));
  return error;
}
