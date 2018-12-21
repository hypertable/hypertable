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

#ifndef Hyperspace_ServerConnectionHandler_h
#define Hyperspace_ServerConnectionHandler_h

#include "Common/Compat.h"

#include "AsyncComm/ApplicationQueue.h"
#include "AsyncComm/DispatchHandler.h"

#include "Master.h"

namespace Hyperspace {

  /*
   *
   */
  class ServerConnectionHandler : public DispatchHandler {
  public:
    ServerConnectionHandler(ApplicationQueuePtr &app_queue, MasterPtr &master);

    virtual void handle(EventPtr &event);

  private:
    Comm *m_comm {};
    ApplicationQueuePtr m_app_queue;
    MasterPtr   m_master;
    uint64_t    m_session_id {};
    gInt32tPtr  m_maintenance_interval;
  };

}

#endif // Hyperspace_ServerConnectionHandler_h
