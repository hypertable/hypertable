/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

#include "Common/Compat.h"

#include <cstdlib>
#include <iostream>

extern "C" {
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
}

#include "Common/Error.h"
#include "Common/Logger.h"
#include "Common/Serialization.h"
#include "Common/System.h"
#include "Common/Time.h"

#include "AsyncComm/Comm.h"

#include "ConnectionManager.h"

using namespace Hypertable;
using namespace std;

void
ConnectionManager::add(const CommAddress &addr, uint32_t timeout_ms,
                       const char *service_name, DispatchHandlerPtr &handler) {
  CommAddress null_addr;
  ConnectionInitializerPtr null_initializer;
  add_internal(addr, null_addr, timeout_ms, service_name, handler, null_initializer);
}

void ConnectionManager::add_with_initializer(const CommAddress &addr,
    uint32_t timeout_ms, const char *service_name,
    DispatchHandlerPtr &handler, ConnectionInitializerPtr &initializer) {
  CommAddress null_addr;
  add_internal(addr, null_addr, timeout_ms, service_name, handler, initializer);
}


void
ConnectionManager::add(const CommAddress &addr, uint32_t timeout_ms,
                       const char *service_name) {
  DispatchHandlerPtr null_disp_handler;
  add(addr, timeout_ms, service_name, null_disp_handler);
}


void
ConnectionManager::add(const CommAddress &addr, const CommAddress &local_addr,
                       uint32_t timeout_ms, const char *service_name,
                       DispatchHandlerPtr &handler) {
  ConnectionInitializerPtr null_initializer;
  add_internal(addr, local_addr, timeout_ms, service_name, handler, null_initializer);
}


void
ConnectionManager::add(const CommAddress &addr, const CommAddress &local_addr,
                       uint32_t timeout_ms, const char *service_name) {
  DispatchHandlerPtr null_disp_handler;
  add(addr, local_addr, timeout_ms, service_name, null_disp_handler);
}

void
ConnectionManager::add_internal(const CommAddress &addr,
          const CommAddress &local_addr, uint32_t timeout_ms,
          const char *service_name, DispatchHandlerPtr &handler,
          ConnectionInitializerPtr &initializer) {
  ScopedLock lock(m_impl->mutex);
  SockAddrMap<ConnectionStatePtr> iter;
  ConnectionState *conn_state;

  HT_ASSERT(addr.is_set());

  if (addr.is_proxy() &&
      m_impl->conn_map_proxy.find(addr.proxy) != m_impl->conn_map_proxy.end())
    return;
  else if (addr.is_inet() &&
	   m_impl->conn_map.find(addr.inet) != m_impl->conn_map.end())
    return;

  conn_state = new ConnectionState();
  conn_state->connected = false;
  conn_state->decomissioned = false;
  conn_state->addr = addr;
  conn_state->local_addr = local_addr;
  conn_state->timeout_ms = timeout_ms;
  conn_state->handler = handler;
  conn_state->initializer = initializer;
  conn_state->initialized = false;
  conn_state->service_name = (service_name) ? service_name : "";
  boost::xtime_get(&conn_state->next_retry, boost::TIME_UTC);

  if (addr.is_proxy())
    m_impl->conn_map_proxy[addr.proxy] = ConnectionStatePtr(conn_state);
  else
    m_impl->conn_map[addr.inet] = ConnectionStatePtr(conn_state);

  {
    ScopedLock conn_lock(conn_state->mutex);
    send_connect_request(conn_state);
  }
}


bool
ConnectionManager::wait_for_connection(const CommAddress &addr,
                                       uint32_t max_wait_ms) {
  Timer timer(max_wait_ms, true);
  return wait_for_connection(addr, timer);
}


bool
ConnectionManager::wait_for_connection(const CommAddress &addr,
                                       Timer &timer) {
  ConnectionStatePtr conn_state_ptr;

  {
    ScopedLock lock(m_impl->mutex);
    if (addr.is_inet()) {
      SockAddrMap<ConnectionStatePtr>::iterator iter =
	m_impl->conn_map.find(addr.inet);
      if (iter == m_impl->conn_map.end())
	return false;
      conn_state_ptr = (*iter).second;
    }
    else if (addr.is_proxy()) {
      hash_map<String, ConnectionStatePtr>::iterator iter = 
	m_impl->conn_map_proxy.find(addr.proxy);
      if (iter == m_impl->conn_map_proxy.end())
	return false;
      conn_state_ptr = (*iter).second;
    }
  }
  
  return wait_for_connection(conn_state_ptr.get(), timer);
}


bool ConnectionManager::wait_for_connection(ConnectionState *conn_state,
					    Timer &timer) {

  timer.start();

  {
    boost::mutex::scoped_lock conn_lock(conn_state->mutex);
    boost::xtime drop_time;

    while (!conn_state->connected && !conn_state->decomissioned) {
      boost::xtime_get(&drop_time, boost::TIME_UTC);
      xtime_add_millis(drop_time, timer.remaining());
      if (!conn_state->cond.timed_wait(conn_lock, drop_time))
        return false;
    }

    if (conn_state->decomissioned)
      return false;

  }

  return true;
}


/**
 * Attempts to establish a connection for the given ConnectionState object.  If
 * a failure occurs, it prints an error message and then schedules a retry by
 * updating the next_retry member of the conn_state object and pushing it onto
 * the retry heap
 *
 * @param conn_state The connection state record
 */
void
ConnectionManager::send_connect_request(ConnectionState *conn_state) {
  int error;
  DispatchHandlerPtr handler(this);

  if (!conn_state->local_addr.is_set())
    error = m_impl->comm->connect(conn_state->addr, handler);
  else
    error = m_impl->comm->connect(conn_state->addr, conn_state->local_addr,
                                  handler);

  if (error == Error::COMM_ALREADY_CONNECTED) {
    conn_state->connected = true;
    conn_state->cond.notify_all();
  }
  else if (error != Error::OK) {
    if (conn_state->service_name != "") {
      HT_INFOF("Connection attempt to %s at %s failed - %s.  Will retry "
               "again in %d milliseconds...", conn_state->service_name.c_str(),
               conn_state->addr.to_str().c_str(), Error::get_text(error),
               (int)conn_state->timeout_ms);
    }
    else {
      HT_INFOF("Connection attempt to service at %s failed - %s.  Will retry "
               "again in %d milliseconds...", conn_state->addr.to_str().c_str(),
               Error::get_text(error), (int)conn_state->timeout_ms);
    }

    // reschedule (throw in a little randomness)
    boost::xtime_get(&conn_state->next_retry, boost::TIME_UTC);
    xtime_add_millis(conn_state->next_retry, conn_state->timeout_ms);

    int32_t milli_adjust = System::rand32() % 2000;
    if (System::rand32() & 1)
      xtime_sub_millis(conn_state->next_retry, milli_adjust);
    else
      xtime_add_millis(conn_state->next_retry, milli_adjust);

    // add to retry heap
    m_impl->retry_queue.push(conn_state);
    m_impl->retry_cond.notify_one();
  }

}


int ConnectionManager::remove(const CommAddress &addr) {
  bool check_inet_addr = false;
  InetAddr inet_addr;
  bool do_close = false;
  int error = Error::OK;

  HT_ASSERT(addr.is_set());

  {
    ScopedLock lock(m_impl->mutex);

    if (addr.is_proxy()) {
      hash_map<String, ConnectionStatePtr>::iterator iter = 
        m_impl->conn_map_proxy.find(addr.proxy);
      if (iter != m_impl->conn_map_proxy.end()) {
	{
	  ScopedLock conn_lock((*iter).second->mutex);
	  check_inet_addr = true;
	  inet_addr = (*iter).second->inet_addr;
	  (*iter).second->decomissioned = true;
          (*iter).second->cond.notify_all();
	  if ((*iter).second->connected)
	    do_close = true;
	  else
	    (*iter).second->connected = true;  // prevent further attempts
	}
	m_impl->conn_map_proxy.erase(iter);
      }
    }
    else if (addr.is_inet()) {
      check_inet_addr = true;
      inet_addr = addr.inet;
    }

    if (check_inet_addr) {
      SockAddrMap<ConnectionStatePtr>::iterator iter =
        m_impl->conn_map.find(inet_addr);
      if (iter != m_impl->conn_map.end()) {
	{
	  ScopedLock conn_lock((*iter).second->mutex);
	  (*iter).second->decomissioned = true;
          (*iter).second->cond.notify_all();
	  if ((*iter).second->connected)
	    do_close = true;
	  else
	    (*iter).second->connected = true;  // prevent further attempts
	}
	m_impl->conn_map.erase(iter);
      }
      
    }
  }

  if (do_close)
    error = m_impl->comm->close_socket(addr);

  return error;
}



/**
 * This is the AsyncComm dispatch handler method.  It gets called for each
 * connection related event (establishment, disconnect, etc.) for each
 * connection.  For connect events, the connection's connected flag is set to
 * true and it's condition variable is signaled.  For all other events (e.g.
 * disconnect or error), the connection's connected flag is set to false and a
 * retry is scheduled.
 *
 * @param event_ptr shared pointer to event object
 */
void
ConnectionManager::handle(EventPtr &event_ptr) {
  ScopedLock lock(m_impl->mutex);
  ConnectionState *conn_state = 0;

  {
    SockAddrMap<ConnectionStatePtr>::iterator iter =
      m_impl->conn_map.find(event_ptr->addr);
    if (iter != m_impl->conn_map.end())
      conn_state = (*iter).second.get();
  }

  if (conn_state == 0 && event_ptr->proxy) {
    hash_map<String, ConnectionStatePtr>::iterator iter = 
      m_impl->conn_map_proxy.find(event_ptr->proxy);
    if (iter != m_impl->conn_map_proxy.end()) {
      conn_state = (*iter).second.get();
      /** register address **/
      m_impl->conn_map[event_ptr->addr] = ConnectionStatePtr(conn_state);
      conn_state->inet_addr = event_ptr->addr;
    }
  }

  if (conn_state) {
    ScopedLock conn_lock(conn_state->mutex);

    if (event_ptr->type == Event::CONNECTION_ESTABLISHED) {
      if (conn_state->initializer) {
        CommBufPtr cbuf(conn_state->initializer->create_initialization_request());
        int error = m_impl->comm->send_request(event_ptr->addr, 60000, cbuf, this);
        if (error != Error::OK)
          HT_FATALF("Problem initializing connection to %s - %s", 
                    conn_state->service_name.c_str(), Error::get_text(error));
      }
      else {
        conn_state->connected = true;
        conn_state->cond.notify_all();
      }
    }
    else if (event_ptr->type == Event::ERROR ||
             event_ptr->type == Event::DISCONNECT) {
      if (!m_impl->quiet_mode) {
        HT_INFOF("%s; Problem connecting to %s, will retry in %d "
                 "milliseconds...", event_ptr->to_str().c_str(),
                 conn_state->service_name.c_str(), (int)conn_state->timeout_ms);
      }
      conn_state->connected = false;
      conn_state->initialized = false;
      // this logic could proably be smarter.  For example, if the last
      // connection attempt was a long time ago, then schedule immediately
      // otherwise, if this event is the result of an immediately prior connect
      // attempt, then do the following
      boost::xtime_get(&conn_state->next_retry, boost::TIME_UTC);
      xtime_add_millis(conn_state->next_retry, conn_state->timeout_ms);

      // add to retry heap
      m_impl->retry_queue.push(conn_state);
      m_impl->retry_cond.notify_one();
    }
    else if (event_ptr->type == Event::MESSAGE) {
      if (conn_state->initializer && !conn_state->initialized) {
        if (event_ptr->header.command != conn_state->initializer->initialization_command()) {
          String err_msg = "Connection initialization not yet complete";
          CommHeader header;
          header.initialize_from_request_header(event_ptr->header);
          CommBufPtr cbuf( new CommBuf(header, 4 + Serialization::encoded_length_str16(err_msg)) );
          cbuf->append_i32(Error::CONNECTION_NOT_INITIALIZED);
          cbuf->append_str16(err_msg);
          m_impl->comm->send_response(event_ptr->addr, cbuf);
          return;
        }
        if (!conn_state->initializer->process_initialization_response(event_ptr.get()))
          HT_FATALF("Unable to initialize connection to %s, exiting ...",
                    conn_state->service_name.c_str());
        conn_state->initialized = true;
        conn_state->connected = true;
        conn_state->cond.notify_all();
        return;
      }
    }

    // Chain event to application supplied handler
    if (conn_state->handler)
      conn_state->handler->handle(event_ptr);
  }
  else {
    HT_WARNF("Unable to find connection for %s in map.",
             InetAddr::format(event_ptr->addr).c_str());
  }
}


/**
 * This is the boost::thread run method.
 */
void ConnectionManager::operator()() {
  ScopedLock lock(m_impl->mutex);
  ConnectionStatePtr conn_state;

  while (!m_impl->shutdown) {

    while (m_impl->retry_queue.empty()) {
      m_impl->retry_cond.wait(lock);
      if (m_impl->shutdown)
        break;
    }

    if (m_impl->shutdown)
      break;

    conn_state = m_impl->retry_queue.top();

    if (conn_state->decomissioned) {
      m_impl->retry_queue.pop();
      continue;
    }

    if (!conn_state->connected) {
      {
        ScopedLock conn_lock(conn_state->mutex);
        boost::xtime now;
        boost::xtime_get(&now, boost::TIME_UTC);

        if (xtime_cmp(conn_state->next_retry, now) <= 0) {
          m_impl->retry_queue.pop();
          send_connect_request(conn_state.get());
          continue;
        }
      }
      m_impl->retry_cond.timed_wait(lock, conn_state->next_retry);
    }
    else
      m_impl->retry_queue.pop();
  }
}


