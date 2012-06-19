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
#include "Common/InetAddr.h"
#include "Common/Time.h"

#include "AsyncComm/ApplicationQueue.h"
#include "AsyncComm/DispatchHandlerSynchronizer.h"
#include "Common/Serialization.h"
#include "Common/Timer.h"

#include "Hyperspace/Session.h"

#include "MasterFileHandler.h"
#include "MasterClient.h"
#include "MasterProtocol.h"

using namespace Hypertable;
using namespace Serialization;


MasterClient::MasterClient(ConnectionManagerPtr &conn_mgr,
    Hyperspace::SessionPtr &hyperspace, const String &toplevel_dir,
    uint32_t timeout_ms, ApplicationQueuePtr &app_queue)
  : m_verbose(true), m_conn_manager(conn_mgr),
    m_hyperspace(hyperspace), m_app_queue(app_queue),
    m_hyperspace_init(false), m_hyperspace_connected(true),
    m_timeout_ms(timeout_ms), m_toplevel_dir(toplevel_dir) {

  m_comm = m_conn_manager->get_comm();
  memset(&m_master_addr, 0, sizeof(m_master_addr));

  m_retry_interval = Config::properties->get_i32("Hypertable.Connection.Retry.Interval");

  /**
   * Open toplevel_dir + /master Hyperspace file to discover the master.
   */
  m_master_file_callback = new MasterFileHandler(this, m_app_queue);
  m_master_file_handle = 0;

  // register hyperspace session callback
  m_hyperspace_session_callback.m_masterclient = this;
  m_hyperspace->add_callback(&m_hyperspace_session_callback);

  // no need to serialize access in ctor
  initialize_hyperspace();
}


MasterClient::MasterClient(ConnectionManagerPtr &conn_mgr, InetAddr &addr, 
                            uint32_t timeout_ms)
  : m_conn_manager(conn_mgr), m_master_addr(addr), m_timeout_ms(timeout_ms) {
  m_comm = m_conn_manager->get_comm();
  m_retry_interval = Config::properties->get_i32("Hypertable.Connection.Retry.Interval");

  m_conn_manager->add_with_initializer(m_master_addr, m_retry_interval, "Master",
                                       m_dispatcher_handler, 
                                       m_connection_initializer);
}


MasterClient::MasterClient(Comm *comm, InetAddr &addr, uint32_t timeout_ms)
  : m_comm(comm), m_master_addr(addr), m_timeout_ms(timeout_ms) {
  m_retry_interval = Config::properties->get_i32("Hypertable.Connection.Retry.Interval");
}


MasterClient::~MasterClient() {
  if (m_hyperspace) {
    if (m_master_file_handle != 0)
      m_hyperspace->close(m_master_file_handle);
    m_hyperspace->remove_callback(&m_hyperspace_session_callback);
  }
}


void MasterClient::initiate_connection(DispatchHandlerPtr dhp, ConnectionInitializerPtr init) {
  m_connection_initializer = init;
  m_dispatcher_handler = dhp;
  if (m_master_file_handle == 0)
    HT_THROW(Error::MASTER_NOT_RUNNING,
             "MasterClient unable to connect to Master");
  reload_master();
}


void MasterClient::hyperspace_disconnected()
{
  ScopedLock lock(m_hyperspace_mutex);
  HT_DEBUG_OUT << "Hyperspace disconnected" << HT_END;
  m_hyperspace_init = false;
  m_hyperspace_connected = false;
}

void MasterClient::hyperspace_reconnected()
{
  ScopedLock lock(m_hyperspace_mutex);
  HT_DEBUG_OUT << "Hyperspace reconnected" << HT_END;
  HT_ASSERT(!m_hyperspace_init);
  m_hyperspace_connected = true;
}

/**
 * Assumes access is serialized via m_hyperspace_mutex
 */
void MasterClient::initialize_hyperspace() {

  if (m_hyperspace_init)
    return;
  HT_ASSERT(m_hyperspace_connected);

  try {
    Timer timer(m_timeout_ms, true);
    m_master_file_handle = m_hyperspace->open(m_toplevel_dir + "/master",
        OPEN_FLAG_READ, m_master_file_callback, &timer);
  }
  catch (Exception &e) {
    if (e.code() != Error::HYPERSPACE_FILE_NOT_FOUND && e.code()
        != Error::HYPERSPACE_BAD_PATHNAME)
      HT_THROW2(e.code(), e, e.what());
  }
  m_hyperspace_init=true;
}

void MasterClient::initialize(Timer *&timer, Timer &tmp_timer) {
  if (timer == 0)
    timer = &tmp_timer;
  timer->start();
}

void
MasterClient::create_namespace(const String &name, int flags, DispatchHandler *handler,
                               Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("create_namespace('%s', 0x%x)", name.c_str(), flags);

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_create_namespace_request(name, flags);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}

void
MasterClient::create_namespace(const String &name, int flags, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("create_namespace('%s', 0x%x)", name.c_str(), flags);

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_create_namespace_request(name, flags);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}

void
MasterClient::drop_namespace(const String &name, bool if_exists,
                             DispatchHandler *handler, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("drop_namespace('%s', if_exists=%s)",
                        name.c_str(), if_exists ? "true" : "false");

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_drop_namespace_request(name, if_exists);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void
MasterClient::drop_namespace(const String &name, bool if_exists, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("drop_namespace('%s', if_exists=%s)",
                        name.c_str(), if_exists ? "true" : "false");

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_drop_namespace_request(name, if_exists);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}

void
MasterClient::create_table(const String &tablename, const String &schema,
                           DispatchHandler *handler, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("create_table('%s')", tablename.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_create_table_request(tablename, schema);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}


void
MasterClient::create_table(const String &tablename, const String &schema,
                           Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("create_table('%s')", tablename.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_create_table_request(tablename, schema);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}

void
MasterClient::alter_table(const String &tablename, const String &schema,
                          DispatchHandler *handler, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("alter_table('%s')", tablename.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_alter_table_request(tablename, schema);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}


void
MasterClient::alter_table(const String &tablename, const String &schema,
                          Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("alter_table('%s')", tablename.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_alter_table_request(tablename, schema);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}

void
MasterClient::get_schema(const String &tablename, DispatchHandler *handler,
                         Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("get_schema('%s')", tablename.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_get_schema_request(tablename);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}


void
MasterClient::get_schema(const String &tablename, std::string &schema,
                         Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("get_schema('%s')", tablename.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_get_schema_request(tablename);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    ptr = event->payload + 4;
    remain = event->payload_len - 4;
    schema = decode_vstr(&ptr, &remain);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void MasterClient::status(Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_status_request();
    if (!send_message(cbp, timer, event, "status"))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, "status");
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation 'status' to master %s failed",
              m_master_addr.format().c_str());
  }

}

void
MasterClient::move_range(const String &source, TableIdentifier *table,
			 RangeSpec &range, const String &log_dir,
			 uint64_t soft_limit, bool split, 
			 DispatchHandler *handler, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  String label = format("move_range(%s[%s..%s], log_dir='%s', soft_limit=%llu)",
                        table->id, range.start_row, range.end_row, log_dir.c_str(),
                        (Llu)soft_limit);

  initialize(timer, tmp_timer);

  cbp = MasterProtocol::create_move_range_request(source, table, range, log_dir,
                                                  soft_limit, split);
  send_message_async(cbp, handler, timer, label);
}


void
MasterClient::move_range(const String &source, TableIdentifier *table,
			 RangeSpec &range, const String &log_dir,
			 uint64_t soft_limit, bool split, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  String label = format("move_range(%s[%s..%s], log_dir='%s', soft_limit=%llu)",
                        table->id, range.start_row, range.end_row, log_dir.c_str(),
                        (Llu)soft_limit);

  initialize(timer, tmp_timer);

  try {
    while (!timer->expired()) {
      cbp = MasterProtocol::create_move_range_request(source, table, range, log_dir,
                                                      soft_limit, split);
      if (!send_message(cbp, timer, event, label))
        continue;
      return;
    }
    {
      ScopedLock lock(m_mutex);
      HT_THROWF(Error::REQUEST_TIMEOUT,
                "MasterClient operation %s to master %s failed", label.c_str(),
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
MasterClient::relinquish_acknowledge(const String &source, TableIdentifier *table,
				     RangeSpec &range, DispatchHandler *handler, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("relinquish_acknowledge(%s[%s..%s])",
                        table->id, range.start_row, range.end_row);

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_relinquish_acknowledge_request(source, table, range);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void
MasterClient::relinquish_acknowledge(const String &source, TableIdentifier *table,
				     RangeSpec &range, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("relinquish_acknowledge(%s[%s..%s])",
                        table->id, range.start_row, range.end_row);

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_relinquish_acknowledge_request(source, table, range);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void
MasterClient::rename_table(const String &old_name, const String &new_name,
                           DispatchHandler *handler, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("rename_table(old='%s', new='%s')", old_name.c_str(), new_name.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_rename_table_request(old_name, new_name);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void
MasterClient::rename_table(const String &old_name, const String &new_name, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("rename_table(old='%s', new='%s')", old_name.c_str(), new_name.c_str());

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_rename_table_request(old_name, new_name);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}

void
MasterClient::drop_table(const String &table_name, bool if_exists,
                         DispatchHandler *handler, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("drop_table('%s', if_exists=%s)",
                        table_name.c_str(), if_exists ? "true" : "false");

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_drop_table_request(table_name, if_exists);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void
MasterClient::drop_table(const String &table_name, bool if_exists, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = format("drop_table('%s', if_exists=%s)",
                        table_name.c_str(), if_exists ? "true" : "false");

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_drop_table_request(table_name, if_exists);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    fetch_result(id, timer, event, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }

}


void MasterClient::shutdown(Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(MasterProtocol::create_shutdown_request());

  initialize(timer, tmp_timer);

  send_message_async(cbp, &sync_handler, timer, "shutdown");

  if (!sync_handler.wait_for_reply(event)) {
    int32_t error = MasterProtocol::response_code(event);
    if (error != Error::COMM_BROKEN_CONNECTION)
      HT_THROW(error, "Master 'shutdown' error");
  }
}

void MasterClient::balance(BalancePlan &plan, DispatchHandler *handler,
                           Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = "balance";

  initialize(timer, tmp_timer);

  while (!timer->expired()) {
    cbp = MasterProtocol::create_balance_request(plan);
    if (!send_message(cbp, timer, event, label))
      continue;
    const uint8_t *ptr = event->payload + 4;
    size_t remain = event->payload_len - 4;
    id = decode_i64(&ptr, &remain);
    cbp = MasterProtocol::create_fetch_result_request(id);
    send_message_async(cbp, handler, timer, label);
    return;
  }

  {
    ScopedLock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "MasterClient operation %s to master %s failed", label.c_str(),
              m_master_addr.format().c_str());
  }
}



void MasterClient::balance(BalancePlan &plan, Timer *timer) {
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = "balance";

  initialize(timer, tmp_timer);

  try {

    while (!timer->expired()) {
      cbp = MasterProtocol::create_balance_request(plan);

      if (!send_message(cbp, timer, event, label))
        continue;
      const uint8_t *ptr = event->payload + 4;
      size_t remain = event->payload_len - 4;
      id = decode_i64(&ptr, &remain);
      break;
    }

    if (timer->expired()) {
      ScopedLock lock(m_mutex);
      HT_THROWF(Error::REQUEST_TIMEOUT,
                "MasterClient operation %s to master %s failed", label.c_str(),
                m_master_addr.format().c_str());
    }

  }
  catch (Exception &e) {
    if (e.code() != Error::MASTER_OPERATION_IN_PROGRESS)
      HT_THROW2(e.code(), e, label);
  }

  fetch_result(id, timer, event, label);

}

void
MasterClient::stop(const String &rsname, bool recover, Timer *timer)
{
  Timer tmp_timer(m_timeout_ms);
  CommBufPtr cbp;
  EventPtr event;
  int64_t id = 0;
  String label = "stop";

  initialize(timer, tmp_timer);

  try {
    while (!timer->expired()) {
      cbp = MasterProtocol::create_stop_request(rsname, recover);

      if (!send_message(cbp, timer, event, label))
        continue;
      const uint8_t *ptr = event->payload + 4;
      size_t remain = event->payload_len - 4;
      id = decode_i64(&ptr, &remain);
      break;
    }

    if (timer->expired()) {
      ScopedLock lock(m_mutex);
      HT_THROWF(Error::REQUEST_TIMEOUT,
                "MasterClient operation %s to master %s failed", label.c_str(),
                m_master_addr.format().c_str());
    }
  }
  catch (Exception &e) {
    if (e.code() != Error::MASTER_OPERATION_IN_PROGRESS)
      HT_THROW2(e.code(), e, label);
  }

  fetch_result(id, timer, event, label);
}

void
MasterClient::send_message_async(CommBufPtr &cbp, DispatchHandler *handler,
                                 Timer *timer, const String &label) {
  boost::mutex::scoped_lock lock(m_mutex);
  int error;

  while ((error = m_comm->send_request(m_master_addr, timer->remaining(), cbp, handler))
      != Error::OK) {
    if (error == Error::COMM_NOT_CONNECTED ||
	error == Error::COMM_BROKEN_CONNECTION ||
	error == Error::COMM_CONNECT_ERROR) {
      boost::xtime expire_time;
      boost::xtime_get(&expire_time, boost::TIME_UTC);
      xtime_add_millis(expire_time, std::min(timer->remaining(), (System::rand32() % m_retry_interval)));
      if (!m_cond.timed_wait(lock, expire_time)) {
        if (timer->expired())
          HT_THROWF(Error::REQUEST_TIMEOUT,
                    "MasterClient operation %s to master %s failed", label.c_str(),
                    m_master_addr.format().c_str());
      }
    }
    else
      HT_THROWF(error, "MasterClient operation %s failed", label.c_str());
  }
}

bool
MasterClient::send_message(CommBufPtr &cbp, Timer *timer, EventPtr &event, const String &label) {
  DispatchHandlerSynchronizer sync_handler;

  send_message_async(cbp, &sync_handler, timer, label);

  if (!sync_handler.wait_for_reply(event)) {
    int32_t error = MasterProtocol::response_code(event);
    if (error == Error::COMM_NOT_CONNECTED ||
        error == Error::COMM_BROKEN_CONNECTION ||
        error == Error::COMM_CONNECT_ERROR) {
      poll(0, 0, std::min(timer->remaining(), (System::rand32() % 3000)));
      return false;
    }
    HT_THROWF(error, "MasterClient operation %s failed", label.c_str());
  }
  return true;
}

void MasterClient::fetch_result(int64_t id, Timer *timer, EventPtr &event, const String &label) {
  CommBufPtr cbp;

  while (!timer->expired()) {
    cbp = MasterProtocol::create_fetch_result_request(id);
    if (!send_message(cbp, timer, event, label))
      continue;
    return;
  }

  {
    boost::mutex::scoped_lock lock(m_mutex);
    HT_THROWF(Error::REQUEST_TIMEOUT,
              "Failed to fetch ID %lld for MasterClient operation %s to master %s",
              (Lld)id, label.c_str(), m_master_addr.format().c_str());
  }
}


void MasterClient::reload_master() {
  InetAddr master_addr;

  {
    ScopedLock lock(m_mutex);
    int error;
    DynamicBuffer value(0);
    String addr_str;

    {
      ScopedLock lock(m_hyperspace_mutex);
      if (m_hyperspace_init)
        m_hyperspace->attr_get(m_master_file_handle, "address", value);
      else if (m_hyperspace_connected) {
        initialize_hyperspace();
        m_hyperspace->attr_get(m_master_file_handle, "address", value);
      }
      else
        HT_THROW(Error::CONNECT_ERROR_HYPERSPACE, "MasterClient not connected to Hyperspace");
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

      m_conn_manager->add_with_initializer(m_master_addr, m_retry_interval, "Master",
                                           m_dispatcher_handler, m_connection_initializer);
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


bool MasterClient::wait_for_connection(uint32_t max_wait_ms) {
  Timer timer(max_wait_ms, true);
  m_conn_manager->wait_for_connection(m_master_addr, timer);
  return m_conn_manager->get_comm()->wait_for_proxy_load(timer);
}

bool MasterClient::wait_for_connection(Timer &timer) {
  m_conn_manager->wait_for_connection(m_master_addr, timer);
  return m_conn_manager->get_comm()->wait_for_proxy_load(timer);
}

void MasterClientHyperspaceSessionCallback::disconnected() {
  m_masterclient->hyperspace_disconnected();
}

void MasterClientHyperspaceSessionCallback::reconnected() {
  m_masterclient->hyperspace_reconnected();
}

