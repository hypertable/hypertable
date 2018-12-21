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

#include "ClientKeepaliveHandler.h"
#include "Master.h"
#include "Protocol.h"
#include "Session.h"

#include <Common/Error.h>
#include <Common/InetAddr.h>
#include <Common/StringExt.h>
#include <Common/Time.h>

#include <chrono>
#include <thread>

using namespace std;
using namespace Hypertable;
using namespace Hyperspace;
using namespace Serialization;

ClientKeepaliveHandler::ClientKeepaliveHandler(Comm *comm, Session *session)
	: m_comm(comm), m_session(session) {

	auto now = chrono::steady_clock::now();
	m_last_keep_alive_send_time = now;
	m_jeopardy_time = now + chrono::milliseconds(m_session->m_lease_interval->get());
	
	String replica = m_session->get_next_replica();

	HT_DEBUG_OUT << "Looking for Hyperspace master at " << replica << ":" << m_session->m_hyperspace_port << HT_END;
	HT_EXPECT(InetAddr::initialize(&m_master_addr, replica.c_str(), m_session->m_hyperspace_port),
		Error::BAD_DOMAIN_NAME);

	m_session->update_master_addr(replica);
}


void ClientKeepaliveHandler::start() {
  int error;

  m_local_addr = InetAddr(INADDR_ANY, m_session->m_datagram_send_port);

  m_comm->create_datagram_receive_socket(m_local_addr, 0x10, shared_from_this());

  CommBufPtr cbp(Hyperspace::Protocol::create_client_keepalive_request(m_session_id,
                                                                       m_delivered_events));

  if ((error = m_comm->send_datagram(m_master_addr, m_local_addr, cbp)
      != Error::OK)) {
    HT_ERRORF("Unable to send datagram - %s", Error::get_text(error));
    exit(EXIT_FAILURE);
  }

  if ((error = m_comm->set_timer(m_session->m_keep_alive_interval->get(), shared_from_this()))
      != Error::OK) {
    HT_ERRORF("Problem setting timer - %s", Error::get_text(error));
    exit(EXIT_FAILURE);
  }

}


void ClientKeepaliveHandler::handle(Hypertable::EventPtr &event) {
  lock_guard<recursive_mutex> lock(m_mutex);
  int error;

  if (m_dead)
    return;
  else if (m_destroying) {
    destroy();
    m_cond_destroyed.notify_all();
    return;
  }

  /*
  if (m_session->m_verbose->get()) {
    HT_INFOF("%s", event->to_str().c_str());
  }
  **/

  if (event->type == Hypertable::Event::MESSAGE) {
    const uint8_t *decode_ptr = event->payload;
    size_t decode_remain = event->payload_len;

    try {

      // sanity check command code
      if (event->header.command >= Protocol::COMMAND_MAX)
        HT_THROWF(Error::PROTOCOL_ERROR, "Invalid command (%llu)",
                  (Llu)event->header.command);

      switch (event->header.command) {
      case Protocol::COMMAND_REDIRECT:
        {
          const char *host;
          host = decode_vstr(&decode_ptr, &decode_remain);

          // if we know who the new master is then send a keepalive to it
          // else send a keepalive to whichever replica we sent it to last time
          if (strlen(host) != 0) {
            HT_DEBUG_OUT << "Received COMMAND_REDIRECT looking for master at "
                        << host << HT_END;
            HT_EXPECT(InetAddr::initialize(&m_master_addr, host, m_session->m_hyperspace_port),
                      Error::BAD_DOMAIN_NAME);
            m_session->update_master_addr(host);
          }

          CommBufPtr cbp(Hyperspace::Protocol::create_client_keepalive_request(m_session_id,
              m_delivered_events));

          if ((error = m_comm->send_datagram(m_master_addr, m_local_addr, cbp) != Error::OK)) {
            HT_ERRORF("Unable to send datagram - %s", Error::get_text(error));
            exit(EXIT_FAILURE);
          }

          if ((error = m_comm->set_timer(m_session->m_keep_alive_interval->get(), shared_from_this())) != Error::OK) {
            HT_ERRORF("Problem setting timer - %s", Error::get_text(error));
            exit(EXIT_FAILURE);
          }
          break;
        }
      case Protocol::COMMAND_KEEPALIVE:
        {
          uint64_t session_id;
          uint32_t notifications;
          uint64_t handle, event_id;
          uint32_t event_mask;
          const char *name;
          const uint8_t *post_notification_buf;
          size_t post_notification_size;

          if (m_session->get_state() == Session::STATE_EXPIRED)
            return;

          // update jeopardy time
          m_jeopardy_time = m_last_keep_alive_send_time +
            chrono::milliseconds(m_session->m_lease_interval->get());

          session_id = decode_i64(&decode_ptr, &decode_remain);
          error = decode_i32(&decode_ptr, &decode_remain);

          if (error != Error::OK) {
            HT_ERRORF("Master session (%llu) error - %s", (Llu)session_id, Error::get_text(error));
            expire_session();
            return;
          }

          if (m_session_id == 0) {
            m_session_id = session_id;
            if (!m_conn_handler) {
              m_conn_handler = make_shared<ClientConnectionHandler>(m_comm, m_session);
              m_conn_handler->set_session_id(m_session_id);
            }
          }

          notifications = decode_i32(&decode_ptr, &decode_remain);

          // Start Issue 313 instrumentation
          post_notification_buf = decode_ptr;
          post_notification_size = decode_remain;

          std::set<uint64_t> delivered_events;

          for (uint32_t i=0; i<notifications; i++) {
            handle = decode_i64(&decode_ptr, &decode_remain);
            event_id = decode_i64(&decode_ptr, &decode_remain);
            event_mask = decode_i32(&decode_ptr, &decode_remain);

            if (m_delivered_events.count(event_id) > 0)
              delivered_events.insert(event_id);

            HandleMap::iterator iter = m_handle_map.find(handle);
            if (iter == m_handle_map.end()) {
              // We have a bad notification, ie. a notification for a handle not in m_handle_map
              auto now = chrono::steady_clock::now();

              HT_ERROR_OUT << "[Issue 313] Received bad notification session=" << m_session_id
                           << ", handle=" << handle << ", event_id=" << event_id
                           << ", event_mask=" << event_mask << HT_END;
              // ignore all notifications in this keepalive message (don't kick up to
              // application) this avoids multiple notifications being sent to app (Issue 314)
              notifications=0;

              // Check if we already have a pending bad notification for this handle
              BadNotificationHandleMap::iterator uiter = m_bad_handle_map.find(handle);
              if (uiter == m_bad_handle_map.end()) {
                // if not then store
                m_bad_handle_map[handle] = now;
              }
              else {
                // if we do then check against grace period
                uint64_t time_diff = chrono::duration_cast<chrono::milliseconds>(now - (*uiter).second).count();
                if (time_diff > ms_bad_notification_grace_period) {
                  HT_ERROR_OUT << "[Issue 313] Still receiving bad notification after grace "
                               << "period=" << ms_bad_notification_grace_period
                               << "ms, session=" << m_session_id
                               << ", handle=" << handle << ", event_id=" << event_id
                               << ", event_mask=" << event_mask << HT_END;
                  HT_ASSERT(false);
                }
              }
              break;
            }
            else {
              // This is a good notification, clear any prev bad notifications for this handle
              BadNotificationHandleMap::iterator uiter = m_bad_handle_map.find(handle);
              if (uiter != m_bad_handle_map.end()) {
                HT_ERROR_OUT << "[Issue 313] Previously bad notification cleared within grace "
                             << "period=" << ms_bad_notification_grace_period
                             << "ms, session=" << m_session_id
                             << ", handle=" << handle << ", event_id=" << event_id
                             << ", event_mask=" << event_mask << HT_END;
                m_bad_handle_map.erase(uiter);
              }
            }

            if (event_mask == EVENT_MASK_ATTR_SET ||
                event_mask == EVENT_MASK_ATTR_DEL ||
                event_mask == EVENT_MASK_CHILD_NODE_ADDED ||
                event_mask == EVENT_MASK_CHILD_NODE_REMOVED)
              decode_vstr(&decode_ptr, &decode_remain);
            else if (event_mask == EVENT_MASK_LOCK_ACQUIRED)
              decode_i32(&decode_ptr, &decode_remain);
            else if (event_mask == EVENT_MASK_LOCK_GRANTED) {
              decode_i32(&decode_ptr, &decode_remain);
              decode_i64(&decode_ptr, &decode_remain);
            }
          }

          m_delivered_events.swap(delivered_events);

          decode_ptr = post_notification_buf;
          decode_remain = post_notification_size;
          // End Issue 313 instrumentation

          for (uint32_t i=0; i<notifications; i++) {
            handle = decode_i64(&decode_ptr, &decode_remain);
            event_id = decode_i64(&decode_ptr, &decode_remain);
            event_mask = decode_i32(&decode_ptr, &decode_remain);

            HandleMap::iterator iter = m_handle_map.find(handle);
            if (iter == m_handle_map.end()) {
              HT_ERROR_OUT << "[Issue 313] this should never happen bad notification session="
                  << m_session_id << ", handle=" << handle << ", event_id=" << event_id
                  << ", event_mask=" << event_mask << HT_END;
              assert(false);
            }

            ClientHandleStatePtr handle_state = (*iter).second;

            if (event_mask == EVENT_MASK_ATTR_SET ||
                event_mask == EVENT_MASK_ATTR_DEL ||
                event_mask == EVENT_MASK_CHILD_NODE_ADDED ||
                event_mask == EVENT_MASK_CHILD_NODE_REMOVED) {
              name = decode_vstr(&decode_ptr, &decode_remain);

              if (!m_delivered_events.insert(event_id).second)
                continue;

              if (handle_state->callback) {
                if (event_mask == EVENT_MASK_ATTR_SET)
                  handle_state->callback->attr_set(name);
                else if (event_mask == EVENT_MASK_ATTR_DEL)
                  handle_state->callback->attr_del(name);
                else if (event_mask == EVENT_MASK_CHILD_NODE_ADDED)
                  handle_state->callback->child_node_added(name);
                else
                  handle_state->callback->child_node_removed(name);
              }
            }
            else if (event_mask == EVENT_MASK_LOCK_ACQUIRED) {
              uint32_t mode = decode_i32(&decode_ptr, &decode_remain);
              if (!m_delivered_events.insert(event_id).second)
                continue;
              if (handle_state->callback)
                handle_state->callback->lock_acquired(mode);
            }
            else if (event_mask == EVENT_MASK_LOCK_RELEASED) {
              if (!m_delivered_events.insert(event_id).second)
                continue;
              if (handle_state->callback)
                handle_state->callback->lock_released();
            }
            else if (event_mask == EVENT_MASK_LOCK_GRANTED) {
              uint32_t mode = decode_i32(&decode_ptr, &decode_remain);
              handle_state->lock_generation = decode_i64(&decode_ptr,
                                                         &decode_remain);
              if (!m_delivered_events.insert(event_id).second)
                continue;
              handle_state->lock_status = LOCK_STATUS_GRANTED;
              handle_state->sequencer->generation =
                  handle_state->lock_generation;
              handle_state->sequencer->mode = mode;
              handle_state->cond.notify_all();
            }

          }
          /*
          if (m_session->m_verbose->get()) {
            HT_INFOF("session_id = %lld", m_session_id);
          }
          **/

          if (m_conn_handler->disconnected())
            m_conn_handler->initiate_connection(m_master_addr);

          if (notifications > 0) {
            CommBufPtr cbp(Protocol::create_client_keepalive_request(
                m_session_id, m_delivered_events));
            m_last_keep_alive_send_time = chrono::steady_clock::now();
            if ((error = m_comm->send_datagram(m_master_addr, m_local_addr, cbp)
                != Error::OK)) {
              HT_ERRORF("Unable to send datagram - %s", Error::get_text(error));
              exit(EXIT_FAILURE);
            }
          }

          m_session->advance_expire_time(m_last_keep_alive_send_time);

          assert(m_session_id == session_id);
        }
        break;
      default:
        HT_THROWF(Error::PROTOCOL_ERROR, "Unimplemented command (%llu)",
                  (Llu)event->header.command);
      }
    }
    catch (Exception &e) {
      HT_ERROR_OUT << e << HT_END;
    }
  }
  else if (event->type == Hypertable::Event::TIMER) {
    int state;

    if ((state = m_session->get_state()) == Session::STATE_EXPIRED)
      return;

    if (state == Session::STATE_SAFE) {
      if (m_jeopardy_time < chrono::steady_clock::now() && !m_session->m_reconnect)
        m_session->state_transition(Session::STATE_JEOPARDY);
    }
    else if (m_session->expired()) {
      expire_session();
      return;
    }

    CommBufPtr cbp(Hyperspace::Protocol::create_client_keepalive_request(
        m_session_id, m_delivered_events));

    m_last_keep_alive_send_time = chrono::steady_clock::now();

    if ((error = m_comm->send_datagram(m_master_addr, m_local_addr, cbp)
        != Error::OK)) {
      HT_ERRORF("Unable to send datagram - %s", Error::get_text(error));
      exit(EXIT_FAILURE);
    }

    if ((error = m_comm->set_timer(m_session->m_keep_alive_interval->get(), shared_from_this()))
        != Error::OK) {
      HT_ERRORF("Problem setting timer - %s", Error::get_text(error));
      exit(EXIT_FAILURE);
    }
  }
  else {
    HT_INFOF("%s", event->to_str().c_str());
  }
}


void ClientKeepaliveHandler::expire_session() {
  m_session->state_transition(m_session->m_reconnect ? Session::STATE_DISCONNECTED : Session::STATE_EXPIRED);

  if (m_conn_handler)
    m_conn_handler->close();
  this_thread::sleep_for(chrono::milliseconds(2000));
  m_conn_handler = 0;
  m_handle_map.clear();
  m_bad_handle_map.clear();
  m_session_id = 0;

  if (m_session->m_reconnect) {
    auto now = chrono::steady_clock::now();
    m_last_keep_alive_send_time = now;
    m_jeopardy_time = now + chrono::milliseconds(m_session->m_lease_interval->get());

    m_local_addr = InetAddr(INADDR_ANY, m_session->m_datagram_send_port);

    m_comm->create_datagram_receive_socket(m_local_addr, 0x10, shared_from_this());

    CommBufPtr cbp(Hyperspace::Protocol::create_client_keepalive_request(
        m_session_id, m_delivered_events));

    int error;
    if ((error = m_comm->send_datagram(m_master_addr, m_local_addr, cbp)
        != Error::OK)) {
      HT_ERRORF("Unable to send datagram - %s", Error::get_text(error));
      exit(EXIT_FAILURE);
    }

    if ((error = m_comm->set_timer(m_session->m_keep_alive_interval->get(), shared_from_this()))
        != Error::OK) {
      HT_ERRORF("Problem setting timer - %s", Error::get_text(error));
      exit(EXIT_FAILURE);
    }
  }
}


void ClientKeepaliveHandler::destroy_session() {
  int error;

  {
    lock_guard<recursive_mutex> lock(m_mutex);
    if (m_dead || m_destroying)
      return;
    m_destroying = true;
    if (m_conn_handler)
      m_conn_handler->disable_callbacks();
  }

  CommBufPtr cbp(Hyperspace::Protocol::create_client_keepalive_request(
                 m_session_id, m_delivered_events, true));

  if ((error = m_comm->send_datagram(m_master_addr, m_local_addr, cbp)
      != Error::OK))
    HT_ERRORF("Unable to send datagram - %s", Error::get_text(error));

  wait_for_destroy_session();

}

void ClientKeepaliveHandler::wait_for_destroy_session() {
  unique_lock<recursive_mutex> lock(m_mutex);
  if (m_dead)
    return;

  m_destroying = true;
  if (m_cond_destroyed.wait_for(lock, chrono::seconds(2)) == cv_status::timeout)
    destroy();
}

void ClientKeepaliveHandler::destroy() {
  if (m_dead)
    return;
  m_dead = true;
  if (m_conn_handler)
    m_conn_handler->close();
  m_conn_handler = 0;
  m_handle_map.clear();
  m_bad_handle_map.clear();
  m_session_id = 0;
  m_comm->close_socket(m_local_addr);
}

