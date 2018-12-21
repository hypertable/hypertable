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

#ifndef Hyperspace_ClientKeepaliveHandler_h
#define Hyperspace_ClientKeepaliveHandler_h

#include <Hyperspace/ClientConnectionHandler.h>
#include <Hyperspace/ClientHandleState.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/DispatchHandler.h>

#include <Common/Time.h>
#include <Common/StringExt.h>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace Hyperspace {

  class Session;

  /*
   *
   */
  class ClientKeepaliveHandler : public DispatchHandler {

  public:
    ClientKeepaliveHandler(Comm *, Session *);

    void start();

    virtual void handle(Hypertable::EventPtr &event);

    void register_handle(ClientHandleStatePtr &handle_state) {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
#ifndef NDEBUG
      HandleMap::iterator iter = m_handle_map.find(handle_state->handle);
      assert(iter == m_handle_map.end());
#endif
      m_handle_map[handle_state->handle] = handle_state;
    }

    void unregister_handle(uint64_t handle) {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      m_handle_map.erase(handle);
    }

    bool get_handle_state(uint64_t handle, ClientHandleStatePtr &handle_state) {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      HandleMap::iterator iter = m_handle_map.find(handle);
      if (iter == m_handle_map.end())
        return false;
      handle_state = (*iter).second;
      return true;
    }

    uint64_t get_session_id() {return m_session_id;}
    void expire_session();

    void destroy_session();
    void wait_for_destroy_session();

  private:

    void destroy();

    std::recursive_mutex m_mutex;
    std::chrono::steady_clock::time_point m_last_keep_alive_send_time;
    std::chrono::steady_clock::time_point m_jeopardy_time;
    bool m_dead {};
    bool m_destroying {};
    std::condition_variable_any m_cond_destroyed;
    Comm *m_comm {};
    sockaddr_in m_master_addr;
    CommAddress m_local_addr;
    Session *m_session {};
    uint64_t m_session_id {};
    ClientConnectionHandlerPtr m_conn_handler;
    std::set<uint64_t> m_delivered_events;
    typedef std::unordered_map<uint64_t, ClientHandleStatePtr> HandleMap;
    HandleMap  m_handle_map;
    typedef std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> BadNotificationHandleMap;
    BadNotificationHandleMap m_bad_handle_map;
    static const uint64_t ms_bad_notification_grace_period = 120000;
    bool m_reconnect {};
  };

  typedef std::shared_ptr<ClientKeepaliveHandler> ClientKeepaliveHandlerPtr;

} // namespace Hypertable

#endif // Hyperspace_ClientKeepaliveHandler_h

