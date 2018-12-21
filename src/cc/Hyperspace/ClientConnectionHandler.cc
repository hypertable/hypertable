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

#include "Common/Compat.h"
#include "Common/Error.h"
#include "Common/InetAddr.h"
#include "Common/System.h"

#include "ClientConnectionHandler.h"
#include "Master.h"
#include "Protocol.h"
#include "Session.h"

using namespace Hyperspace;
using namespace Hypertable;
using namespace std;

ClientConnectionHandler::ClientConnectionHandler(Comm *comm, Session *session) 
  : m_comm(comm), m_session(session), m_session_id(0), m_state(DISCONNECTED),
    m_callbacks_enabled(true) {
  memset(&m_master_addr, 0, sizeof(struct sockaddr_in));
}


ClientConnectionHandler::~ClientConnectionHandler() {
  /** was causing deadlock ...
  if (m_master_addr.sin_port != 0)
    m_comm->close_socket(m_master_addr);
  */
}


void ClientConnectionHandler::handle(Hypertable::EventPtr &event_ptr) {
  lock_guard<recursive_mutex> lock(m_mutex);
  int error;

  HT_DEBUGF("%s", event_ptr->to_str().c_str());

  if (event_ptr->type == Hypertable::Event::MESSAGE) {

    if (Protocol::response_code(event_ptr.get()) != Error::OK)
      HT_FATALF("Connection handshake error: %s",
                Protocol::string_format_message(event_ptr.get()).c_str());

    if (m_callbacks_enabled)
      m_session->state_transition(Session::STATE_SAFE);

    m_state = CONNECTED;
  }
  else if (event_ptr->type == Hypertable::Event::DISCONNECT) {

    if (m_session->m_verbose->get()) {
      HT_WARNF("%s", event_ptr->to_str().c_str());
    }

    if (m_callbacks_enabled)
      m_session->state_transition(Session::STATE_JEOPARDY);

    m_state = DISCONNECTED;
    m_cond.notify_all();
  }
  else if (event_ptr->type == Hypertable::Event::CONNECTION_ESTABLISHED) {

    m_state = HANDSHAKING;

    memcpy(&m_master_addr, &event_ptr->addr, sizeof(struct sockaddr_in));

    CommBufPtr
        cbp(Hyperspace::Protocol::create_handshake_request(m_session_id, System::exe_name));

    if ((error = m_comm->send_request(event_ptr->addr, 
                                      m_session->m_lease_interval->get(), cbp, this))
        != Error::OK) {
      HT_FATALF("Problem sending handshake request - %s",
                Error::get_text(error));
    }
  }
  else
    HT_FATALF("Unrecognized event - %s", event_ptr->to_str().c_str());

}

void ClientConnectionHandler::close() {
  unique_lock<recursive_mutex> lock(m_mutex);
  m_comm->close_socket(m_master_addr);
  m_cond.wait(lock, [this](){ return m_state == DISCONNECTED; });
}
