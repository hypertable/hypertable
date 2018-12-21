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

#include <Common/Compat.h>

#include "Session.h"

#include <Hyperspace/ClientHandleState.h>
#include <Hyperspace/Master.h>
#include <Hyperspace/Protocol.h>

#include <AsyncComm/DispatchHandlerSynchronizer.h>
#include <AsyncComm/Comm.h>
#include <AsyncComm/ConnectionManager.h>

#include <Common/Config.h>
#include <Common/Error.h>
#include <Common/InetAddr.h>
#include <Common/Logger.h>
#include <Common/Serialization.h>
#include <Common/SleepWakeNotifier.h>
#include <Common/Time.h>

#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>

#include <cassert>
#include <chrono>

using namespace Hypertable;
using namespace Hyperspace;
using namespace Serialization;


Session::Session(Comm *comm, PropertiesPtr &props)
  : m_comm(comm), m_props(props), m_state(STATE_JEOPARDY), m_last_callback_id(0) {

  m_hyperspace_replicas = m_props->get_ptr<gStrings>("Hyperspace.Replica.Host");

  m_datagram_send_port = props->get_i16("Hyperspace.Client.Datagram.SendPort");
  m_hyperspace_port = props->get_i16("Hyperspace.Replica.Port");

  m_keep_alive_interval = props->get_ptr<gInt32t>("Hyperspace.KeepAlive.Interval");
  m_lease_interval = props->get_ptr<gInt32t>("Hyperspace.Lease.Interval");
  m_grace_period = props->get_ptr<gInt32t>("Hyperspace.GracePeriod");
  
  m_verbose = props->get_ptr<gBool>("Hypertable.Verbose");
  m_silent = props->has("Hypertable.Silent") && props->get_bool("Hypertable.Silent");

  m_reconnect = props->get_bool("Hyperspace.Session.Reconnect");

  if (m_reconnect)
    HT_INFO("Hyperspace session setup to reconnect");

  // m_timeout_ms = m_lease_interval->get() * 2;

  m_expire_time = chrono::steady_clock::now() +
	  chrono::milliseconds(m_grace_period->get());

  m_keepalive_handler_ptr = std::make_shared<ClientKeepaliveHandler>(m_comm, this);
  m_keepalive_handler_ptr->start();

  function<void()> sleep_callback = [this]() -> void {this->handle_sleep();};
  function<void()> wakeup_callback = [this]() -> void {this->handle_wakeup();};
  m_sleep_wake_notifier = new SleepWakeNotifier(sleep_callback, wakeup_callback);
}

Session::~Session() {
  if (m_keepalive_handler_ptr)
    m_keepalive_handler_ptr->destroy_session();
  delete m_sleep_wake_notifier;
}

String Session::get_next_replica() 
{
	// opts, rnd or rack aware
	// if round-rubin
	m_hyperspace_replica_nxt++;
	if (m_hyperspace_replica_nxt >= m_hyperspace_replicas->size())
		m_hyperspace_replica_nxt = 0;
	return m_hyperspace_replicas->get_item(m_hyperspace_replica_nxt);
}


void Session::update_master_addr(const String &host)
{
  lock_guard<mutex> lock(m_mutex);
  HT_EXPECT(InetAddr::initialize(&m_master_addr, host.c_str(), m_hyperspace_port),
            Error::BAD_DOMAIN_NAME);
  m_hyperspace_master = host;
}

void Session::handle_sleep() {
  lock_guard<mutex> lock(m_mutex);
  m_expire_time = chrono::steady_clock::now() +
    chrono::milliseconds(m_grace_period->get());
}

void Session::handle_wakeup() {
  {
    lock_guard<mutex> lock(m_mutex);
    m_expire_time = chrono::steady_clock::now() +
      chrono::milliseconds(m_grace_period->get());
  }
  if (m_state == Session::STATE_JEOPARDY)
    state_transition(Session::STATE_SAFE);
}


void Session::shutdown(Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_shutdown_request());

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr))
      HT_THROW((int)Protocol::response_code(event_ptr.get()),
               "Hyperspace 'shutdown' error");
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }

  m_keepalive_handler_ptr->wait_for_destroy_session();
  m_keepalive_handler_ptr = 0;
}

void Session::add_callback(SessionCallback *cb)
{
  lock_guard<mutex> lock(m_callback_mutex);
  ++m_last_callback_id;
  m_callbacks[m_last_callback_id] = cb;
  cb->set_id(m_last_callback_id);
}

bool Session::remove_callback(SessionCallback *cb)
{
  lock_guard<mutex> lock(m_callback_mutex);
  return m_callbacks.erase(cb->get_id());
}

uint64_t
Session::open(ClientHandleStatePtr &handle_state, CommBufPtr &cbuf_ptr,
              Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;

  handle_state->handle = 0;
  handle_state->sequencer = 0;
  handle_state->lock_status = 0;
  uint32_t open_flags = handle_state->open_flags;

  if ((open_flags & OPEN_FLAG_LOCK_SHARED) == OPEN_FLAG_LOCK_SHARED)
    handle_state->lock_mode = LOCK_MODE_SHARED;
  else if ((open_flags & OPEN_FLAG_LOCK_EXCLUSIVE) == OPEN_FLAG_LOCK_EXCLUSIVE)
    handle_state->lock_mode = LOCK_MODE_EXCLUSIVE;
  else
    handle_state->lock_mode = 0;

 try_again:
  if (!wait_for_safe())
    return Error::HYPERSPACE_EXPIRED_SESSION;

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      error = (int)Protocol::response_code(event_ptr.get());
      HT_THROWF(error,  "Hyperspace 'open' error, name=%s flags=0x%x events=0x"
                "%x", handle_state->normal_name.c_str(), open_flags,
                handle_state->event_mask);
    }
    else {
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      handle_state->handle = decode_i64(&decode_ptr, &decode_remain);
      decode_byte(&decode_ptr, &decode_remain);
      handle_state->lock_generation = decode_i64(&decode_ptr, &decode_remain);
      /** if (createdp) *createdp = cbyte ? true : false; **/
      m_keepalive_handler_ptr->register_handle(handle_state);
      HT_DEBUG_OUT << "Open succeeded session="
                  << m_keepalive_handler_ptr->get_session_id()
                  << ", name=" << handle_state->normal_name
                  << ", handle=" << handle_state->handle << ", flags=" << open_flags
                  << ", event_mask=" << handle_state->event_mask << HT_END;

      return handle_state->handle;
    }
  }

  state_transition(Session::STATE_JEOPARDY);
  goto try_again;

}


uint64_t
Session::open(const std::string &name, uint32_t flags,
              HandleCallbackPtr &callback, Timer *timer) {
  ClientHandleStatePtr handle_state(new ClientHandleState());
  std::vector<Attribute> empty_attrs;

  handle_state->open_flags = flags;
  handle_state->event_mask = (callback) ? callback->get_event_mask() : 0;
  handle_state->callback = callback;

  normalize_name(name, handle_state->normal_name);

  CommBufPtr cbuf_ptr(Protocol::create_open_request(handle_state->normal_name,
                      flags, callback, empty_attrs));

  return open(handle_state, cbuf_ptr, timer);
}

uint64_t
Session::open(const std::string &name, uint32_t flags, Timer *timer) {
  HandleCallbackPtr null_handle_callback;
  return open(name, flags, null_handle_callback, timer);
}


uint64_t
Session::create(const std::string &name, uint32_t flags,
                HandleCallbackPtr &callback,
                const std::vector<Attribute> &init_attrs, Timer *timer) {
  ClientHandleStatePtr handle_state(new ClientHandleState());

  handle_state->open_flags = flags | OPEN_FLAG_CREATE | OPEN_FLAG_EXCL;
  handle_state->event_mask = (callback) ? callback->get_event_mask() : 0;
  handle_state->callback = callback;
  normalize_name(name, handle_state->normal_name);

  CommBufPtr cbuf_ptr(Protocol::create_open_request(handle_state->normal_name,
                      handle_state->open_flags, callback, init_attrs));

  return open(handle_state, cbuf_ptr, timer);
}


/*
 *
 */
void Session::close(uint64_t handle, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_close_request(handle));

  try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr))
      HT_THROW((int)Protocol::response_code(event_ptr.get()),
               "Hyperspace 'close' error");
    m_keepalive_handler_ptr->unregister_handle(handle);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void Session::close_nowait(uint64_t handle) {
  {
    lock_guard<mutex> lock(m_mutex);
    if (m_state != STATE_SAFE)
      return;
  }
  CommBufPtr cbuf_ptr(Protocol::create_close_request(handle));
  send_message(cbuf_ptr, 0, 0);
}


/*
 *
 */
void Session::mkdir(const std::string &name, const std::vector<Attribute> &init_attrs, Timer *timer) {
  mkdir(name, false, &init_attrs, timer);
}

void Session::mkdir(const std::string &name, Timer *timer) {
  mkdir(name, false, 0, timer);
}

void Session::mkdirs(const std::string &name, Timer *timer) {
  mkdir(name, true, 0, timer);
}

void Session::mkdirs(const std::string &name, const std::vector<Attribute> &init_attrs, Timer *timer) {
  mkdir(name, true, &init_attrs, timer);
}


void Session::unlink(const std::string &name, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  String normal_name;

  normalize_name(name, normal_name);

  CommBufPtr cbuf_ptr(Protocol::create_delete_request(normal_name));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr))
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Hyperspace 'unlink' error, name=%s", normal_name.c_str());
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }

}


bool Session::exists(const std::string &name, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  String normal_name;

  normalize_name(name, normal_name);

  CommBufPtr cbuf_ptr(Protocol::create_exists_request(normal_name));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Hyperspace 'exists' error, name=%s", normal_name.c_str());
    }
    else {
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      uint8_t bval = decode_byte(&decode_ptr, &decode_remain);
      return (bval == 0) ? false : true;
    }
  }

  state_transition(Session::STATE_JEOPARDY);
  goto try_again;
}


/*
 */
void Session::attr_set(uint64_t handle, const std::string &attr,
                       const void *value, size_t value_len, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_set_request(handle, 0, 0, attr, value,
                      value_len));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      ClientHandleStatePtr handle_state;
      String fname = "UNKNOWN";
      if (m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
        fname = handle_state->normal_name.c_str();
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem setting attribute '%s' of hyperspace file '%s'",
                attr.c_str(), fname.c_str());
    }
    return;
  }

  state_transition(Session::STATE_JEOPARDY);
  goto try_again;
}

/*
 */
void Session::attr_set(uint64_t handle, const std::vector<Attribute> &attrs,
                       Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_set_request(handle, 0, 0, attrs));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      ClientHandleStatePtr handle_state;
      String fname = "UNKNOWN";
      if (m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
        fname = handle_state->normal_name.c_str();
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem setting attributes of hyperspace file '%s'", fname.c_str());
    }
    return;
  }

  state_transition(Session::STATE_JEOPARDY);
  goto try_again;
}

/*
 */
void Session::attr_set(const std::string &name, const std::string &attr,
                       const void *value, size_t value_len, Timer *timer) {
  attr_set(name, 0, attr, value, value_len, timer);
}

void Session::attr_set(const std::string &name, uint32_t oflags, const std::string &attr,
                       const void *value, size_t value_len, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_set_request(0, &name, oflags, attr, value,
                      value_len));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem setting attribute '%s' of hyperspace file '%s'",
                attr.c_str(), name.c_str());
    }
    return;
  }

  state_transition(Session::STATE_JEOPARDY);
  goto try_again;
}

void Session::attr_set(const std::string &name, uint32_t oflags,
                       const std::vector<Attribute> &attrs, Timer *timer) {

  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_set_request(0, &name, oflags, attrs));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem setting attributes of hyperspace file '%s'", name.c_str());
    }
    return;
  }

  state_transition(Session::STATE_JEOPARDY);
  goto try_again;
}

/*
 */
uint64_t Session::attr_incr(uint64_t handle, const std::string &attr, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_incr_request(handle, 0, attr));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      ClientHandleStatePtr handle_state;
      String fname = "UNKNOWN";
      if (m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
        fname = handle_state->normal_name.c_str();
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem incrementing attribute '%s' of hyperspace file '%s'",
                attr.c_str(), fname.c_str());
    }
    else {
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      uint64_t attr_val = decode_i64(&decode_ptr, &decode_remain);

      return attr_val;
    }
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }

}

/*
 */
uint64_t Session::attr_incr(const std::string &name, const std::string &attr, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_incr_request(0, &name, attr));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem incrementing attribute '%s' of hyperspace file '%s'",
                attr.c_str(), name.c_str());
    }
    else {
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      uint64_t attr_val = decode_i64(&decode_ptr, &decode_remain);

      return attr_val;
    }
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }

}

void
Session::attr_get(uint64_t handle, const std::string &attr,
                  DynamicBuffer &value, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_get_request(handle, 0, attr));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      ClientHandleStatePtr handle_state;
      String fname = "UNKNOWN";
      if (m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
        fname = handle_state->normal_name.c_str();
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem getting attribute '%s' of hyperspace file '%s'",
                attr.c_str(), fname.c_str());
    }
    else
      decode_value(event_ptr, value);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void
Session::attr_get(const std::string &name, const std::string &attr,
                  DynamicBuffer &value, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_get_request(0, &name, attr));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem getting attribute '%s' of hyperspace file '%s'",
                attr.c_str(), name.c_str());
    }
    else
      decode_value(event_ptr, value);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void
Session::attr_get(const std::string &name, const std::string &attr,
                  bool& attr_exists, DynamicBuffer &value, Timer *timer)
{
  attr_exists = false;
  try {
      attr_get(name, attr, value, timer);
      attr_exists = true;
    }
    catch (Exception &e) {
      if (e.code() != Error::HYPERSPACE_ATTR_NOT_FOUND)
        throw;
    }
}

void
Session::attrs_get(uint64_t handle, const std::vector<std::string> &attrs,
                  std::vector<DynamicBufferPtr> &values, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attrs_get_request(handle, 0, attrs));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      ClientHandleStatePtr handle_state;
      String fname = "UNKNOWN";
      if (m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
        fname = handle_state->normal_name.c_str();
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem getting attributes of hyperspace file '%s'",
                fname.c_str());
    }
    else
      decode_values(event_ptr, values);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void
Session::attrs_get(const std::string &name, const std::vector<std::string> &attrs,
                  std::vector<DynamicBufferPtr> &values, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attrs_get_request(0, &name, attrs));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem getting attributes of hyperspace file '%s'",
                 name.c_str());
    }
    else
      decode_values(event_ptr, values);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

bool
Session::attr_exists(uint64_t handle, const std::string& attr, Timer *timer)
{
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;

  CommBufPtr cbuf_ptr(Protocol::create_attr_exists_request(handle, attr));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Hyperspace 'attr_exists' error, name=%s", attr.c_str());
    }
    else {
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      uint8_t bval = decode_byte(&decode_ptr, &decode_remain);
      return (bval == 0) ? false : true;
    }
  }

  state_transition(Session::STATE_JEOPARDY);
  goto try_again;
}

bool
Session::attr_exists(const std::string& name, const std::string& attr, Timer *timer)
{
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;

  CommBufPtr cbuf_ptr(Protocol::create_attr_exists_request(name, attr));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Hyperspace 'attr_exists' error, name=%s", attr.c_str());
    }
    else {
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      uint8_t bval = decode_byte(&decode_ptr, &decode_remain);
      return (bval == 0) ? false : true;
    }
  }

  state_transition(Session::STATE_JEOPARDY);
  goto try_again;
}

/*
 *
 */
void Session::attr_del(uint64_t handle, const std::string &name, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_attr_del_request(handle, name));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      ClientHandleStatePtr handle_state;
      String fname = "UNKNOWN";
      if (m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
        fname = handle_state->normal_name.c_str();
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Problem deleting attribute '%s' of hyperspace file '%s'",
                name.c_str(), fname.c_str());
    }
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }

}

void Session::attr_list(uint64_t handle, vector<String> &anames, Timer *timer) {
   DispatchHandlerSynchronizer sync_handler;
   Hypertable::EventPtr event_ptr;
   CommBufPtr cbuf_ptr(Protocol::create_attr_list_request(handle));

  try_again:
   if (!wait_for_safe())
     HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

   int error = send_message(cbuf_ptr, &sync_handler, timer);
   if (error == Error::OK) {
     if (!sync_handler.wait_for_reply(event_ptr)) {
       ClientHandleStatePtr handle_state;
       String fname = "UNKNOWN";
       if (m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
         fname = handle_state->normal_name.c_str();
       HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                 "Problem getting list of attributes of hyperspace file '%s'",
                 fname.c_str());
     }
     else {
       const uint8_t *decode_ptr = event_ptr->payload + 4;
       size_t decode_remain = event_ptr->payload_len - 4;

       uint32_t num_attributes = decode_i32(&decode_ptr, &decode_remain);

       for (uint32_t k = 0; k < num_attributes; k++) {
         String attrname;
         attrname = decode_vstr(&decode_ptr, &decode_remain);
         anames.push_back(attrname);
       }
     }
   }
   else {
     state_transition(Session::STATE_JEOPARDY);
     goto try_again;
   }

}


void
Session::readdir(uint64_t handle, std::vector<DirEntry> &listing,
                 Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_readdir_request(handle));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROW((int)Protocol::response_code(event_ptr.get()),
               "Hyperspace 'readdir' error");
    }
    else {
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      uint32_t entry_cnt;
      DirEntry dentry;
      try {
        entry_cnt = decode_i32(&decode_ptr, &decode_remain);
      }
      catch (Exception &e) {
        HT_THROW2(Error::PROTOCOL_ERROR, e, "");
      }
      listing.clear();
      for (uint32_t i=0; i<entry_cnt; i++) {
        try {
          decode_dir_entry(&decode_ptr, &decode_remain, dentry);
        }
        catch (Exception &e) {
          HT_THROW2F(Error::PROTOCOL_ERROR, e,
                     "Problem decoding entry %d of READDIR return packet", i);
        }
        listing.push_back(dentry);
      }
    }
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void
Session::readdir_attr(uint64_t handle, const std::string &attr, bool include_sub_entries,
                      std::vector<DirEntryAttr> &listing, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_readdir_attr_request(handle, 0, attr, include_sub_entries));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROW((int)Protocol::response_code(event_ptr.get()),
               "Hyperspace 'readdir_attr' error");
    }
    else
      decode_listing(event_ptr, listing);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void
Session::readdir_attr(const std::string &name, const std::string &attr, bool include_sub_entries,
                      std::vector<DirEntryAttr> &listing, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_readdir_attr_request(0, &name, attr, include_sub_entries));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROW((int)Protocol::response_code(event_ptr.get()),
               "Hyperspace 'readdir_attr' error");
    }
    else
      decode_listing(event_ptr, listing);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void
Session::readpath_attr(uint64_t handle, const std::string &attr,
                      std::vector<DirEntryAttr> &listing, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_readpath_attr_request(handle, 0, attr));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROW((int)Protocol::response_code(event_ptr.get()),
               "Hyperspace 'readpath_attr' error");
    }
    else
      decode_listing(event_ptr, listing);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void
Session::readpath_attr(const std::string &name, const std::string &attr,
                      std::vector<DirEntryAttr> &listing, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_readpath_attr_request(0, &name, attr));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr)) {
      HT_THROW((int)Protocol::response_code(event_ptr.get()),
               "Hyperspace 'readpath_attr' error");
    }
    else
      decode_listing(event_ptr, listing);
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void
Session::lock(uint64_t handle, LockMode mode, LockSequencer *sequencerp,
              Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_lock_request(handle, mode, false));
  ClientHandleStatePtr handle_state;
  uint32_t status = 0;

  if (!m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
    HT_THROW(Error::HYPERSPACE_INVALID_HANDLE, "");

  if (handle_state->lock_status != 0)
    HT_THROW(Error::HYPERSPACE_ALREADY_LOCKED, "");

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  {
    lock_guard<mutex> lock(handle_state->mutex);
    sequencerp->mode = mode;
    sequencerp->name = handle_state->normal_name;
    handle_state->sequencer = sequencerp;
  }

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr))
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Hyperspace 'lock' error, name='%s'",
                handle_state->normal_name.c_str());
    else {
      unique_lock<mutex> lock(handle_state->mutex);
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      handle_state->lock_mode = mode;
      try {
        status = decode_i32(&decode_ptr, &decode_remain);

        if (status == LOCK_STATUS_GRANTED) {
          sequencerp->generation = decode_i64(&decode_ptr, &decode_remain);
          handle_state->lock_generation = sequencerp->generation;
          handle_state->lock_status = LOCK_STATUS_GRANTED;
        }
        else {
          assert(status == LOCK_STATUS_PENDING);
          handle_state->lock_status = LOCK_STATUS_PENDING;
          handle_state->cond.wait(lock, [&handle_state](){ return handle_state->lock_status != LOCK_STATUS_PENDING; });
          if (handle_state->lock_status == LOCK_STATUS_CANCELLED)
            HT_THROW(Error::HYPERSPACE_REQUEST_CANCELLED, "");
          assert(handle_state->lock_status == LOCK_STATUS_GRANTED);
        }
      }
      catch (Exception &e) {
        HT_THROW2(e.code(), e, "lock response decode error");
      }
    }
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }

}


void
Session::try_lock(uint64_t handle, LockMode mode, LockStatus *statusp,
                  LockSequencer *sequencerp, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_lock_request(handle, mode, true));
  ClientHandleStatePtr handle_state;

  if (!m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
    HT_THROW(Error::HYPERSPACE_INVALID_HANDLE, "");

  if (handle_state->lock_status != 0)
    HT_THROW(Error::HYPERSPACE_ALREADY_LOCKED, "");

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr))
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Hyperspace 'try_lock' error, name='%s'",
                handle_state->normal_name.c_str());
    else {
      lock_guard<mutex> lock(handle_state->mutex);
      const uint8_t *decode_ptr = event_ptr->payload + 4;
      size_t decode_remain = event_ptr->payload_len - 4;
      try {
        *statusp = (LockStatus)decode_i32(&decode_ptr, &decode_remain);

        if (*statusp == LOCK_STATUS_GRANTED) {
          sequencerp->generation = decode_i64(&decode_ptr, &decode_remain);
          sequencerp->mode = mode;
          sequencerp->name = handle_state->normal_name;
          handle_state->lock_mode = mode;
          handle_state->lock_status = LOCK_STATUS_GRANTED;
          handle_state->lock_generation = sequencerp->generation;
          handle_state->sequencer = 0;
        }
      }
      catch (Exception &e) {
        HT_THROW2(e.code(), e, "try_lock response decode error");
      }
    }
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }

}


void Session::release(uint64_t handle, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_release_request(handle));
  ClientHandleStatePtr handle_state;

  if (!m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
    HT_THROW(Error::HYPERSPACE_INVALID_HANDLE, "");

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    lock_guard<mutex> lock(handle_state->mutex);
    if (!sync_handler.wait_for_reply(event_ptr))
      HT_THROW((int)Protocol::response_code(event_ptr.get()),
               "Hyperspace 'release' error");
    handle_state->lock_status = 0;
    handle_state->cond.notify_all();
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }

}



void
Session::get_sequencer(uint64_t handle, LockSequencer *sequencerp,
                       Timer *timer) {
  ClientHandleStatePtr handle_state;

  if (!m_keepalive_handler_ptr->get_handle_state(handle, handle_state))
    HT_THROW(Error::HYPERSPACE_INVALID_HANDLE, "");

  if (handle_state->lock_generation == 0)
    HT_THROW(Error::HYPERSPACE_NOT_LOCKED, "");

  sequencerp->name = handle_state->normal_name;
  sequencerp->mode = handle_state->lock_mode;
  sequencerp->generation = handle_state->lock_generation;

}


/*
 */
void Session::check_sequencer(LockSequencer &sequencer, Timer *timer) {
  HT_WARN("CheckSequencer not implemented.");
}

/*
data based on client
 */
String Session::locate(int type) {
  String location;

  switch(type) {
    case LOCATE_MASTER:
      location = m_hyperspace_master +  "\n";
      break;
  
    case LOCATE_REPLICAS:
      for(size_t idx=0; idx<m_hyperspace_replicas->size(); idx++)
        location.append(get_next_replica() + "\n");
      break;
  }
  return location;
}
/*
data based on client
 */
String Session::cfg_reload(const String &filename) {
	return format("\n%s\n", Config::reparse_file(filename).c_str());
} /// ht_hyperspace cfg_reload command chk up (only the client's cfg reloaded )


/*
 */
int Session::status(Status &status, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  EventPtr event_ptr;
  CommBufPtr cbuf_ptr(Protocol::create_status_request());
  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr))
      error = (int)Protocol::response_code(event_ptr);
    else {
      const uint8_t *ptr = event_ptr->payload + 4;
      size_t remain = event_ptr->payload_len - 4;
      status.decode(&ptr, &remain);
      error = Error::OK;
    }
  }
  return error;
}


int Session::state_transition(int state) {
  lock_guard<mutex> lock(m_mutex);
  int old_state = m_state;
  m_state = state;
  if (m_state == STATE_SAFE) {
    m_cond.notify_all();
    if (old_state == STATE_JEOPARDY) {
      for(CallbackMap::iterator it = m_callbacks.begin(); it != m_callbacks.end(); it++)
        (it->second)->safe();
    }
    else if (old_state == STATE_DISCONNECTED)
      for(CallbackMap::iterator it = m_callbacks.begin(); it != m_callbacks.end(); it++)
        (it->second)->reconnected();
  }
  else if (m_state == STATE_JEOPARDY) {
    if (old_state == STATE_SAFE) {
      for(CallbackMap::iterator it = m_callbacks.begin(); it != m_callbacks.end(); it++)
        (it->second)->jeopardy();
      m_expire_time = chrono::steady_clock::now() +
        chrono::milliseconds(m_grace_period->get());
    }
  }
  else if (m_state == STATE_DISCONNECTED) {
    if (m_reconnect) {
      if (old_state != STATE_DISCONNECTED)
        for(CallbackMap::iterator it = m_callbacks.begin(); it != m_callbacks.end(); it++)
          (it->second)->disconnected();
      m_expire_time = chrono::steady_clock::now() +
        chrono::milliseconds(m_grace_period->get());
    }
  }
  else if (m_state == STATE_EXPIRED) {
    if (old_state != STATE_EXPIRED) {
      for(CallbackMap::iterator it = m_callbacks.begin(); it != m_callbacks.end(); it++)
        (it->second)->expired();
    }
    m_cond.notify_all();
  }
  return old_state;
}


int Session::get_state() {
  lock_guard<mutex> lock(m_mutex);
  return m_state;
}


bool Session::expired() {
  lock_guard<mutex> lock(m_mutex);
  return m_expire_time < chrono::steady_clock::now();
}


bool Session::wait_for_connection(uint32_t max_wait_ms) {
  unique_lock<mutex> lock(m_mutex);
  auto drop_time = chrono::steady_clock::now() + chrono::milliseconds(max_wait_ms);
  return m_cond.wait_until(lock, drop_time,
                           [this]{ return m_state == STATE_SAFE; });
}


bool Session::wait_for_connection(Timer &timer) {
  unique_lock<mutex> lock(m_mutex);
  auto drop_time = chrono::steady_clock::now() + chrono::milliseconds(timer.remaining());
  return m_cond.wait_until(lock, drop_time,
                           [this]{ return m_state == STATE_SAFE; });
}


void Session::mkdir(const std::string &name, bool create_intermediate, const std::vector<Attribute> *init_attrs, Timer *timer) {
  DispatchHandlerSynchronizer sync_handler;
  Hypertable::EventPtr event_ptr;
  String normal_name;

  normalize_name(name, normal_name);

  CommBufPtr cbuf_ptr(Protocol::create_mkdir_request(normal_name, create_intermediate, init_attrs));

 try_again:
  if (!wait_for_safe())
    HT_THROW(Error::HYPERSPACE_EXPIRED_SESSION, "");

  int error = send_message(cbuf_ptr, &sync_handler, timer);
  if (error == Error::OK) {
    if (!sync_handler.wait_for_reply(event_ptr))
      HT_THROWF((int)Protocol::response_code(event_ptr.get()),
                "Hyperspace 'mkdir' error, name=%s", normal_name.c_str());
  }
  else {
    state_transition(Session::STATE_JEOPARDY);
    goto try_again;
  }
}

void Session::decode_value(Hypertable::EventPtr& event_ptr, DynamicBuffer &value) {
  uint32_t attr_val_len = 0;
  const uint8_t *decode_ptr = event_ptr->payload + 8;
  size_t decode_remain = event_ptr->payload_len - 8;
  try {
    void *attr_val = decode_bytes32(&decode_ptr, &decode_remain,
                                    &attr_val_len);
    value.clear();
    value.ensure(attr_val_len+1);
    value.add_unchecked(attr_val, attr_val_len);
    // nul-terminate to make caller's lives easier
    *value.ptr = 0;
  }
  catch (Exception &e) {
    HT_THROW2(Error::PROTOCOL_ERROR, e, "");
  }
}

void Session::decode_values(Hypertable::EventPtr& event_ptr, std::vector<DynamicBufferPtr> &values) {
  values.clear();
  uint32_t attr_val_len = 0;
  const uint8_t *decode_ptr = event_ptr->payload + 4;
  size_t decode_remain = event_ptr->payload_len - 4;
  try {
    uint32_t attr_val_cnt = decode_i32(&decode_ptr, &decode_remain);
    values.reserve(attr_val_cnt);
    while (attr_val_cnt-- > 0) {
      DynamicBufferPtr value;
      void *attr_val = decode_bytes32(&decode_ptr, &decode_remain,
                                      &attr_val_len);
      if (attr_val_len) {
        value = make_shared<DynamicBuffer>(attr_val_len+1);
        value->add_unchecked(attr_val, attr_val_len);
        // nul-terminate to make caller's lives easier
        *value->ptr = 0;
      }
      values.push_back(value);
    }
  }
  catch (Exception &e) {
    HT_THROW2(Error::PROTOCOL_ERROR, e, "");
  }
}

void Session::decode_listing(Hypertable::EventPtr& event_ptr, std::vector<DirEntryAttr> &listing) {
  const uint8_t *decode_ptr = event_ptr->payload + 4;
  size_t decode_remain = event_ptr->payload_len - 4;
  uint32_t entry_cnt;
  DirEntryAttr dentry;
  try {
    entry_cnt = decode_i32(&decode_ptr, &decode_remain);
  }
  catch (Exception &e) {
    HT_THROW2(Error::PROTOCOL_ERROR, e, "");
  }
  listing.clear();
  listing.reserve(entry_cnt);
  for (uint32_t ii=0; ii<entry_cnt; ii++) {
    try {
      decode_dir_entry_attr(&decode_ptr, &decode_remain, dentry);
    }
    catch (Exception &e) {
      HT_THROW2F(Error::PROTOCOL_ERROR, e,
                  "Problem decoding entry %d of READDIR_ATTR return packet", ii);
    }
    listing.push_back(dentry);
  }
}

bool Session::wait_for_safe() {
  unique_lock<mutex> lock(m_mutex);
  while (m_state != STATE_SAFE) {
    if (m_state == STATE_EXPIRED)
      return false;
    m_cond.wait(lock);
  }
  return true;
}

int
Session::send_message(CommBufPtr &cbuf_ptr, DispatchHandler *handler,
                      Timer *timer) {
  lock_guard<mutex> lock(m_mutex);
  int error;
  uint32_t timeout_ms = timer ? (time_t)timer->remaining() 
                              : m_lease_interval->get() * 2;

  if ((error = m_comm->send_request(m_master_addr, timeout_ms, cbuf_ptr,
      handler)) != Error::OK) {
    std::string str;
    if (!m_silent)
      HT_WARNF("Comm::send_request to htHyperspace at %s failed - %s",
               m_master_addr.format().c_str(), Error::get_text(error));
  }
  return error;
}


void Session::normalize_name(const String &name, String &normal) {

  if (name == "/") {
    normal = name;
    return;
  }

  normal = "";
  if (name[0] != '/')
    normal += "/";

  if (name.find('/', name.length()-1) == string::npos)
    normal += name;
  else
    normal += name.substr(0, name.length()-1);
}

HsCommandInterpreterPtr Session::create_hs_interpreter() {
  return make_shared<HsCommandInterpreter>(this);
}

void Hyperspace::close_handle(SessionPtr hyperspace, uint64_t handle) {
  if (handle)
    hyperspace->close(handle);
}

void Hyperspace::close_handle_ptr(SessionPtr hyperspace, uint64_t *handlep) {
  if (*handlep)
    hyperspace->close(*handlep);
}
