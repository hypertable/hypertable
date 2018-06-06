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
 * Definitions for MasterClient
 * This file contains definitions for MasterClient, a client interface class
 * to the Master.
 */

#include <Common/Compat.h>

#include "Client.h"
#include "EventHandlerMasterChange.h"
#include "HyperspaceCallback.h"
#include "NamespaceFlag.h"
#include "Protocol.h"
#include "Request/Parameters/AlterTable.h"
#include "Request/Parameters/Balance.h"
#include "Request/Parameters/Compact.h"
#include "Request/Parameters/CreateNamespace.h"
#include "Request/Parameters/CreateTable.h"
#include "Request/Parameters/DropNamespace.h"
#include "Request/Parameters/DropTable.h"
#include "Request/Parameters/FetchResult.h"
#include "Request/Parameters/MoveRange.h"
#include "Request/Parameters/PhantomCommitComplete.h"
#include "Request/Parameters/PhantomPrepareComplete.h"
#include "Request/Parameters/RecreateIndexTables.h"
#include "Request/Parameters/RelinquishAcknowledge.h"
#include "Request/Parameters/RenameTable.h"
#include "Request/Parameters/ReplayComplete.h"
#include "Request/Parameters/ReplayStatus.h"
#include "Request/Parameters/SetState.h"
#include "Request/Parameters/Stop.h"
#include "Response/Parameters/Status.h"
#include "Response/Parameters/SystemStatus.h"

#include <Hyperspace/Session.h>

#include <AsyncComm/ApplicationQueueInterface.h>
#include <AsyncComm/DispatchHandlerSynchronizer.h>
#include <AsyncComm/Protocol.h>

#include <Common/Config.h>
#include <Common/Error.h>
#include <Common/InetAddr.h>
#include <Common/Random.h>
#include <Common/Serialization.h>
#include <Common/Time.h>
#include <Common/Timer.h>

#include <chrono>
#include <thread>

using namespace Hypertable;
using namespace Hypertable::Lib;
using namespace Serialization;
using namespace std;

Master::Client::Client(ConnectionManagerPtr &conn_mgr,
                       Hyperspace::SessionPtr &hyperspace,
                       const String &toplevel_dir, uint32_t timeout_ms,
                       ApplicationQueueInterfacePtr &app_queue,
                       DispatchHandlerPtr dhp,ConnectionInitializerPtr init)
  : m_conn_manager(conn_mgr), m_hyperspace(hyperspace), m_app_queue(app_queue),
    m_dispatcher_handler(dhp), m_connection_initializer(init),
    m_timeout_ms(timeout_ms), m_toplevel_dir(toplevel_dir) {

  m_comm = m_conn_manager->get_comm();
  memset(reinterpret_cast<void*>(&m_master_addr), 0, sizeof(m_master_addr));

  m_retry_interval = Config::properties->get_i32("Hypertable.Connection.Retry.Interval");
  m_verbose = Config::get_bool("verbose");

  /**
   * Open toplevel_dir + /master Hyperspace file to discover the master.
   */
  m_master_file_callback = make_shared<HyperspaceCallback>(this, m_app_queue);

  // register hyperspace session callback
  m_hyperspace_session_callback.m_client = this;
  m_hyperspace->add_callback(&m_hyperspace_session_callback);

  m_hyperspace_connected = true;
  initialize_hyperspace();
  reload_master();
}

Master::Client::Client(ConnectionManagerPtr &conn_mgr, InetAddr &addr, 
                           uint32_t timeout_ms)
  : m_conn_manager(conn_mgr), m_master_addr(addr), m_timeout_ms(timeout_ms) {
  m_comm = m_conn_manager->get_comm();
  m_retry_interval = Config::properties->get_i32("Hypertable.Connection.Retry.Interval");
  m_verbose = Config::get_bool("verbose");
  m_conn_manager->add_with_initializer(m_master_addr, m_retry_interval, "Master",
                                       m_dispatcher_handler,
                                       m_connection_initializer);
}


Master::Client::Client(Comm *comm, InetAddr &addr, uint32_t timeout_ms)
  : m_comm(comm), m_master_addr(addr), m_timeout_ms(timeout_ms) {
  m_retry_interval = Config::properties->get_i32("Hypertable.Connection.Retry.Interval");
  m_verbose = Config::get_bool("verbose");
}


Master::Client::~Client() {
  if (m_hyperspace) {
    m_hyperspace->remove_callback(&m_hyperspace_session_callback);
    if (m_master_file_handle != 0)
      m_hyperspace->close_nowait(m_master_file_handle);
  }
}


void Master::Client::hyperspace_disconnected()
{
  lock_guard<mutex> lock(m_hyperspace_mutex);
  HT_DEBUG_OUT << "Hyperspace disconnected" << HT_END;
  m_hyperspace_init = false;
  m_hyperspace_connected = false;
}

void Master::Client::hyperspace_reconnected()
{
  {
    lock_guard<mutex> lock(m_hyperspace_mutex);
    HT_DEBUG_OUT << "Hyperspace reconnected" << HT_END;
    HT_ASSERT(!m_hyperspace_init);
    m_hyperspace_connected = true;
  }

  EventPtr nullevent;
  m_app_queue->add(new EventHandlerMasterChange(this, nullevent));
}

/**
 * Assumes access is serialized via m_hyperspace_mutex
 */
void Master::Client::initialize_hyperspace() {

  if (m_hyperspace_init)
    return;
  HT_ASSERT(m_hyperspace_connected);

  Timer timer(m_timeout_ms, true);
  while (timer.remaining()) {
    try {
      m_master_file_handle =
        m_hyperspace->open(m_toplevel_dir + "/master",
                           OPEN_FLAG_READ|OPEN_FLAG_CREATE,
                           m_master_file_callback, &timer);
      break;
    }
    catch (Exception &e) {
      if (e.code() != Error::HYPERSPACE_FILE_NOT_FOUND)
        throw;
      this_thread::sleep_for(chrono::milliseconds(3000));
    }
  }
  if (m_master_file_handle == 0)
    HT_THROW(Error::REQUEST_TIMEOUT, "Opening hyperspace master file");
  m_hyperspace_init = true;
}

void Master::Client::initialize(Timer *&timer, Timer &tmp_timer) {
  if (timer == 0)
    timer = &tmp_timer;
  timer->start();
}


void
Master::Client::create_namespace(const String &name, int32_t flags, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("create_namespace('%s', flags=%s)", name.c_str(),
                        NamespaceFlag::to_str(flags).c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_CREATE_NAMESPACE);
      Request::Parameters::CreateNamespace params(name, flags);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);

    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void
Master::Client::drop_namespace(const String &name, int32_t flags, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id = 0;
  String label = format("drop_namespace('%s', flags=%s)",
                        name.c_str(), NamespaceFlag::to_str(flags).c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_DROP_NAMESPACE);
      Request::Parameters::DropNamespace params(name, flags);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void Master::Client::compact(const String &tablename, const String &row,
                     int32_t range_types, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  String label = format("compact('%s')", tablename.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_COMPACT);
      Request::Parameters::Compact params(tablename, row, range_types);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    int64_t id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}


void
Master::Client::create_table(const String &name, const String &schema,
                           Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id = 0;
  String label = format("create_table('%s')", name.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_CREATE_TABLE);
      Request::Parameters::CreateTable params(name, schema);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}


void
Master::Client::alter_table(const String &name, const String &schema,
                          bool force, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id = 0;
  String label = format("alter_table('%s')", name.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_ALTER_TABLE);
      Request::Parameters::AlterTable params(name, schema, force);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}

void Master::Client::status(Status &status, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    CommHeader header(Protocol::COMMAND_STATUS);
    CommBufPtr cbuf( new CommBuf(header, 0) );
    if (!send_message(cbuf, timer, event, "status"))
      continue;

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    Response::Parameters::Status params;
    params.decode(&ptr, &remain);
    status = params.status();
    return;
  }

  lock_guard<mutex> lock(m_mutex);
  HT_THROWF(Error::REQUEST_TIMEOUT,
            "Client operation 'status' to master %s failed",
            m_master_addr.format().c_str());
}


void
Master::Client::move_range(const String &source, int64_t range_id,
                           TableIdentifier &table,
                           RangeSpec &range, const String &transfer_log,
                           uint64_t soft_limit, bool split, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  String label =
    format("move_range(%s[%s..%s] (%lld), transfer_log='%s', soft_limit=%llu)",
           table.id, range.start_row, range.end_row, (Lld)range_id,
           transfer_log.c_str(), (Llu)soft_limit);

  initialize(timer, tmp_timer);

  try {
    while (!timer->expired()) {

      {
        CommHeader header(Protocol::COMMAND_MOVE_RANGE);
        Request::Parameters::MoveRange params(source, range_id, table, range,
                                              transfer_log, soft_limit, split);
        CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
        params.encode(cbuf->get_data_ptr_address());
        if (!send_message(cbuf, timer, event, label))
          continue;
      }
      return;
    }
    {
      lock_guard<mutex> lock(m_mutex);
      HT_THROWF(Error::REQUEST_TIMEOUT,
                "Client operation %s to master %s failed", label.c_str(),
                m_master_addr.format().c_str());
    }
  }
  catch (Exception &e) {
    if (e.code() == Error::MASTER_OPERATION_IN_PROGRESS)
      return;
    HT_THROW2(e.code(), e, label);
  }
}


void
Master::Client::relinquish_acknowledge(const String &source, int64_t range_id,
                                       TableIdentifier &table,
                                       RangeSpec &range, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id = 0;
  String label = format("relinquish_acknowledge(%s[%s..%s] id=%lld)",
                        table.id, range.start_row, range.end_row,(Lld)range_id);

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_RELINQUISH_ACKNOWLEDGE);
      Request::Parameters::RelinquishAcknowledge params(source, range_id, table, range);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}



void
Master::Client::rename_table(const String &from, const String &to, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id = 0;
  String label = format("rename_table(old='%s', new='%s')", from.c_str(), to.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_RENAME_TABLE);
      Request::Parameters::RenameTable params(from, to);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}


void
Master::Client::drop_table(const String &name, bool if_exists, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  String label = format("drop_table('%s', if_exists=%s)",
                        name.c_str(), if_exists ? "true" : "false");

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_DROP_TABLE);
      Request::Parameters::DropTable params(name, if_exists);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    int64_t id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}

void Master::Client::recreate_index_tables(const std::string &name,
                                   TableParts parts, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id = 0;
  String label = format("recreate_index_tables('%s', part=%s)",
                        name.c_str(), parts.to_string().c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    {
      CommHeader header(Protocol::COMMAND_RECREATE_INDEX_TABLES);
      Request::Parameters::RecreateIndexTables params(name, parts);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}



void Master::Client::shutdown(Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;

  initialize(timer, tmp_timer);

  CommHeader header(Protocol::COMMAND_SHUTDOWN);
  CommBufPtr cbuf( new CommBuf(header, 0) );
  send_message_async(cbuf, &sync_handler, timer, "shutdown");

  if (!sync_handler.wait_for_reply(event)) {
    int32_t error = Hypertable::Protocol::response_code(event);
    if (error != Error::COMM_BROKEN_CONNECTION)
      HT_THROW(error, "Master 'shutdown' error");
  }
}


void Master::Client::balance(BalancePlan &plan, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id = 0;
  String label = "balance";

  initialize(timer, tmp_timer);

  try {

    while (!timer->expired()) {

      {
        CommHeader header(Protocol::COMMAND_BALANCE);
        Request::Parameters::Balance params(plan);
        CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
        params.encode(cbuf->get_data_ptr_address());
        if (!send_message(cbuf, timer, event, label))
          continue;
      }
      const uint8_t *ptr = event->payload + 4;
      size_t remain = event->payload_len - 4;
      id = decode_i64(&ptr, &remain);
      break;
    }

    if (timer->expired()) {
      lock_guard<mutex> lock(m_mutex);
      HT_THROWF(Error::REQUEST_TIMEOUT,
                "Client operation %s to master %s failed", label.c_str(),
                m_master_addr.format().c_str());
    }

  }
  catch (Exception &e) {
    if (e.code() != Error::MASTER_OPERATION_IN_PROGRESS)
      HT_THROW2(e.code(), e, label);
  }

  fetch_result(id, timer, event, label);

}


void Master::Client::set_state(const std::vector<SystemVariable::Spec> &specs,
                               Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id = 0;
  String label = "set";

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    {
      CommHeader header(Protocol::COMMAND_SET_STATE);
      Request::Parameters::SetState params(specs);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
    }
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Client operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}


void Master::Client::stop(const String &rsname, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;
  int64_t id {};
  String label = "stop";

  initialize(timer, tmp_timer);

  try {
    while (!timer->expired()) {
      CommHeader header(Protocol::COMMAND_STOP);
      Request::Parameters::Stop params(rsname);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, timer, event, label))
        continue;
      const uint8_t *ptr = event->payload + 4;
      size_t remain = event->payload_len - 4;
      id = decode_i64(&ptr, &remain);
      break;
    }

    if (timer->expired()) {
      lock_guard<mutex> lock(m_mutex);
      HT_THROWF(Error::REQUEST_TIMEOUT,
                "Client operation %s to master %s failed", label.c_str(),
                m_master_addr.format().c_str());
    }
  }
  catch (Exception &e) {
    if (e.code() != Error::MASTER_OPERATION_IN_PROGRESS)
      HT_THROW2(e.code(), e, label);
  }

  fetch_result(id, timer, event, label);
}


void Master::Client::system_status(Status &status, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  EventPtr event;

  initialize(timer, tmp_timer);

  while (!timer->expired()) {

    CommHeader header(Protocol::COMMAND_SYSTEM_STATUS);
    CommBufPtr cbuf( new CommBuf(header, 0) );
    if (!send_message(cbuf, timer, event, "system status"))
      continue;

    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    Response::Parameters::SystemStatus params;
    params.decode(&ptr, &remain);
    status = params.status();
    return;
  }

  lock_guard<mutex> lock(m_mutex);
  HT_THROWF(Error::REQUEST_TIMEOUT,
            "Client operation 'system status' to master %s failed",
            m_master_addr.format().c_str());
}


void
Master::Client::send_message_async(CommBufPtr &cbp, DispatchHandler *handler,
                                 Timer *timer, const String &label) {
  unique_lock<mutex> lock(m_mutex);
  DispatchHandlerSynchronizer *sync_handler
    = dynamic_cast<DispatchHandlerSynchronizer *>(handler);
  int error;

  while ((error = m_comm->send_request(m_master_addr, timer->remaining(), cbp,
                                       handler)) != Error::OK) {
    // COMM_BROKEN_CONNECTION implies handler will be called back, if handler
    // is a DispatchHandlerSynchronizer, wait for callback and try again
    if (error == Error::COMM_BROKEN_CONNECTION) {
      if (sync_handler) {
        EventPtr event;
        sync_handler->wait_for_reply(event);
      }
      else
        return;
    }

    auto expire_time = chrono::system_clock::now() +
      chrono::milliseconds(std::min(timer->remaining(),
                                    Random::number32(m_retry_interval)));

    if (m_cond.wait_until(lock, expire_time) == cv_status::timeout) {
      if (timer->expired())
        HT_THROWF(Error::REQUEST_TIMEOUT,
                  "Client operation %s to master %s failed", label.c_str(),
                  m_master_addr.format().c_str());
    }
  }
}

bool
Master::Client::send_message(CommBufPtr &cbp, Timer *timer, EventPtr &event, const String &label) {
  DispatchHandlerSynchronizer sync_handler;

  send_message_async(cbp, &sync_handler, timer, label);

  if (!sync_handler.wait_for_reply(event)) {
    int32_t error = Hypertable::Protocol::response_code(event);
    if (error == Error::COMM_NOT_CONNECTED ||
        error == Error::COMM_BROKEN_CONNECTION ||
        error == Error::COMM_CONNECT_ERROR ||
        error == Error::SERVER_NOT_READY) {
      this_thread::sleep_for(chrono::milliseconds(std::min(timer->remaining(), Random::number32(3000))));
      return false;
    }
    HT_THROWF(error, "Client operation %s failed", label.c_str());
  }
  return true;
}

void Master::Client::fetch_result(int64_t id, Timer *timer, EventPtr &event, const String &label) {

  while (!timer->expired()) {

    CommHeader header(Protocol::COMMAND_FETCH_RESULT);
    Request::Parameters::FetchResult params(id);
    CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
    params.encode(cbuf->get_data_ptr_address());
    if (!send_message(cbuf, timer, event, label))
      continue;
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Failed to fetch ID %lld for Client operation %s to master %s",
              (Lld)id, label.c_str(), m_master_addr.format().c_str());
  }
}

void
Master::Client::replay_status(int64_t op_id, const String &location,
                      int32_t plan_generation) {
  Timer timer(m_timeout_ms, true);
  EventPtr event;
  String label = format("replay_status op_id=%llu location=%s "
                        "plan_generation=%d", (Llu)op_id,
                        location.c_str(), plan_generation);

  while (!timer.expired()) {

    {
      CommHeader header(Protocol::COMMAND_REPLAY_STATUS);
      Request::Parameters::ReplayStatus params(op_id, location, plan_generation);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, &timer, event, label)) {
        this_thread::sleep_for(chrono::milliseconds(std::min(timer.remaining(), Random::number32(3000))));
        continue;
      }
    }
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
        "Client operation %s to master %s failed", label.c_str(),
        m_master_addr.format().c_str());
  }
}


void
Master::Client::replay_complete(int64_t op_id, const String &location,
                        int32_t plan_generation, int32_t error,
                        const String message) {
  Timer timer(m_timeout_ms, true);
  EventPtr event;
  String label = format("replay_complete op_id=%llu location=%s "
                        "plan_generation=%d error=%s", (Llu)op_id,
                        location.c_str(), plan_generation,
                        Error::get_text(error));

  while (!timer.expired()) {

    {
      CommHeader header(Protocol::COMMAND_REPLAY_COMPLETE);
      Request::Parameters::ReplayComplete params(op_id, location,
                                                 plan_generation,
                                                 error, message);
      CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
      params.encode(cbuf->get_data_ptr_address());
      if (!send_message(cbuf, &timer, event, label)) {
        this_thread::sleep_for(chrono::milliseconds(std::min(timer.remaining(), Random::number32(3000))));
        continue;
      }
    }
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
        "Client operation %s to master %s failed", label.c_str(),
        m_master_addr.format().c_str());
  }
}

void
Master::Client::phantom_prepare_complete(int64_t op_id, const String &location,
                                 int plan_generation, int32_t error,
                                 const String message) {
  Timer timer(m_timeout_ms, true);
  EventPtr event;
  String label = format("phantom_prepare_complete op_id=%llu location=%s "
                        "plan_generation=%d error=%s", (Llu)op_id,
                        location.c_str(), plan_generation,
                        Error::get_text(error));

  while (!timer.expired()) {
    CommHeader header(Protocol::COMMAND_PHANTOM_PREPARE_COMPLETE);
    Request::Parameters::PhantomPrepareComplete params(op_id, location,
                                                       plan_generation, error,
                                                       message);
    CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
    params.encode(cbuf->get_data_ptr_address());
    if (!send_message(cbuf, &timer, event, label)) {
      this_thread::sleep_for(chrono::milliseconds(std::min(timer.remaining(), Random::number32(3000))));
      continue;
    }
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
        "Client operation %s to master %s failed", label.c_str(),
        m_master_addr.format().c_str());
  }
}

void
Master::Client::phantom_commit_complete(int64_t op_id, const String &location,
                                int plan_generation, int32_t error,
                                const String message) {
  Timer timer(m_timeout_ms, true);
  EventPtr event;
  String label = format("phantom_commit_complete op_id=%llu location=%s "
                        "error=%s", (Llu)op_id, location.c_str(), Error::get_text(error));

  while (!timer.expired()) {
    CommHeader header(Protocol::COMMAND_PHANTOM_COMMIT_COMPLETE);
    Request::Parameters::PhantomCommitComplete params(op_id, location,
                                                      plan_generation, error,
                                                      message);
    CommBufPtr cbuf( new CommBuf(header, params.encoded_length()) );
    params.encode(cbuf->get_data_ptr_address());
    if (!send_message(cbuf, &timer, event, label)) {
      this_thread::sleep_for(chrono::milliseconds(std::min(timer.remaining(), Random::number32(3000))));
      continue;
    }
    return;
  }

  {
    lock_guard<mutex> lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
        "Client operation %s to master %s failed", label.c_str(),
        m_master_addr.format().c_str());
  }
}

void Master::Client::reload_master() {
  InetAddr master_addr;

  {
    unique_lock<mutex> lock(m_mutex);
    int error;
    DynamicBuffer value(0);
    String addr_str;

    {
      lock_guard<mutex> lock(m_hyperspace_mutex);
      if (m_hyperspace_init) {
        try {
          m_hyperspace->attr_get(m_master_file_handle, "address", value);
        }
        catch (Exception &e) {
          HT_WARN("Unable to determine master address from Hyperspace");
          return;
        }
      }
      else if (m_hyperspace_connected) {
        initialize_hyperspace();
        try {
          m_hyperspace->attr_get(m_master_file_handle, "address", value);
        }
        catch (Exception &e) {
          HT_WARN("Unable to determine master address from Hyperspace");
          return;
        }
      }
      else
        HT_THROW(Error::CONNECT_ERROR_HYPERSPACE, "Client not connected to Hyperspace");
    }
    addr_str = (const char *)value.base;

    if (addr_str != m_master_addr_string) {

      if (m_master_addr.sin_port != 0) {
        if ((error = m_conn_manager->remove(m_master_addr)) != Error::OK) {
          if (m_verbose)
            HT_WARNF("Problem removing connection to Master - %s",
                     Error::get_text(error));
        }
        if (m_verbose)
          HT_INFOF("Connecting to new Master (old=%s, new=%s)",
                   m_master_addr_string.c_str(), addr_str.c_str());
      }

      m_master_addr_string = addr_str;

      InetAddr::initialize(&m_master_addr, m_master_addr_string.c_str());

      // If the new master is not yet fully initialized, the connect() can
      // fail. In this case the clients can run into a timeout before the
      // master attempts to re-connect. To avoid that, the retry interval is
      // cut in half.
      m_conn_manager->add_with_initializer(m_master_addr, m_retry_interval / 2,
              "Master", m_dispatcher_handler, m_connection_initializer);
    }
    master_addr = m_master_addr;
  }

  {
    Timer timer(m_retry_interval, true);
    if (m_conn_manager->wait_for_connection(master_addr, timer))
      m_conn_manager->get_comm()->wait_for_proxy_load(timer);
    m_cond.notify_all();
  }

}


bool Master::Client::wait_for_connection(uint32_t max_wait_ms) {
  Timer timer(max_wait_ms, true);
  m_conn_manager->wait_for_connection(m_master_addr, timer);
  return m_conn_manager->get_comm()->wait_for_proxy_load(timer);
}

bool Master::Client::wait_for_connection(Timer &timer) {
  m_conn_manager->wait_for_connection(m_master_addr, timer);
  return m_conn_manager->get_comm()->wait_for_proxy_load(timer);
}

void Master::ClientHyperspaceSessionCallback::disconnected() {
  m_client->hyperspace_disconnected();
}

void Master::ClientHyperspaceSessionCallback::reconnected() {
  m_client->hyperspace_reconnected();
}
