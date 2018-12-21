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

#ifndef Hyperspace_ClientConnectionHandler_h
#define Hyperspace_ClientConnectionHandler_h

#include <boost/thread/condition.hpp>

#include <AsyncComm/Comm.h>
#include <AsyncComm/DispatchHandler.h>

#include <Common/Error.h>
#include <Common/InetAddr.h>

#include <condition_variable>
#include <mutex>

namespace Hyperspace {

  using namespace Hypertable;

  class Session;

  class ClientConnectionHandler : public DispatchHandler {
  public:

    enum { DISCONNECTED, CONNECTING, HANDSHAKING, CONNECTED };

    ClientConnectionHandler(Comm *comm, Session *session);
    virtual ~ClientConnectionHandler();

    virtual void handle(Hypertable::EventPtr &event_ptr);

    void set_session_id(uint64_t id) { m_session_id = id; }

    bool disconnected() {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      return m_state == DISCONNECTED;
    }

    int initiate_connection(struct sockaddr_in &addr) {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      m_state = CONNECTING;
      int error = m_comm->connect(addr, shared_from_this());

      if (error == Error::COMM_ALREADY_CONNECTED) {
        HT_WARNF("Connection attempt to htHyperspace "
                 "at %s - %s", InetAddr::format(addr).c_str(),
                 Error::get_text(error));
        error = Error::OK;
      }

      if (error != Error::OK) {
        HT_ERRORF("Problem establishing TCP connection with htHyperspace "
                  "at %s - %s", InetAddr::format(addr).c_str(),
                  Error::get_text(error));
        m_comm->close_socket(addr);
        m_state = DISCONNECTED;
      }

      return error;
    }

    void disable_callbacks() { m_callbacks_enabled = false; }

    void close();

  private:
    std::recursive_mutex m_mutex;
    std::condition_variable_any m_cond;
    Comm *m_comm {};
    Session *m_session {};
    uint64_t m_session_id {};
    int m_state {};
    struct sockaddr_in m_master_addr;
    bool m_callbacks_enabled {};
  };

  /// Shared smart pointer to ClientConnectionHandler
  typedef std::shared_ptr<ClientConnectionHandler> ClientConnectionHandlerPtr;

}

#endif // Hyperspace_ClientConnectionHandler_h
