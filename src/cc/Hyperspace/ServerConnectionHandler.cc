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
#include "Common/Config.h"
#include "Common/Error.h"
#include "Common/StringExt.h"

#include "AsyncComm/ApplicationQueue.h"
#include "AsyncComm/Comm.h"

#include "Protocol.h"
#include "request/RequestHandlerAttrSet.h"
#include "request/RequestHandlerAttrGet.h"
#include "request/RequestHandlerAttrIncr.h"
#include "request/RequestHandlerAttrDel.h"
#include "request/RequestHandlerAttrExists.h"
#include "request/RequestHandlerAttrList.h"
#include "request/RequestHandlerMkdir.h"
#include "request/RequestHandlerDelete.h"
#include "request/RequestHandlerOpen.h"
#include "request/RequestHandlerClose.h"
#include "request/RequestHandlerExists.h"
#include "request/RequestHandlerReaddir.h"
#include "request/RequestHandlerReaddirAttr.h"
#include "request/RequestHandlerReadpathAttr.h"
#include "request/RequestHandlerLock.h"
#include "request/RequestHandlerRelease.h"
#include "request/RequestHandlerStatus.h"
#include "request/RequestHandlerHandshake.h"
#include "request/RequestHandlerDoMaintenance.h"
#include "request/RequestHandlerDestroySession.h"
#include "request/RequestHandlerShutdown.h"
#include "ServerConnectionHandler.h"

using namespace std;
using namespace Hypertable;
using namespace Hyperspace;
using namespace Serialization;
using namespace Error;

ServerConnectionHandler::ServerConnectionHandler(ApplicationQueuePtr &app_queue,
                                                 MasterPtr &master)
  : m_app_queue(app_queue), m_master(master), m_session_id(0) {
  m_comm = Comm::instance();
  m_maintenance_interval = Config::properties->get_ptr<gInt32t>("Hyperspace.Maintenance.Interval");
}



/*
 *
 */
void ServerConnectionHandler::handle(EventPtr &event) {

  //HT_INFOF("%s", event->to_str().c_str());

  if (event->type == Hypertable::Event::MESSAGE) {
    ApplicationHandler *handler = 0;

    try {

      // sanity check command code
      if (event->header.command >= Protocol::COMMAND_MAX)
        HT_THROWF(Error::PROTOCOL_ERROR, "Invalid command (%llu)",
                  (Llu)event->header.command);

      // if this is not the current replication master then try to return
      // addr of current master
      if (!m_master->is_master())
        HT_THROW(Error::HYPERSPACE_NOT_MASTER_LOCATION, (String) "Current master=" +
            m_master->get_current_master());

      switch (event->header.command) {
         case Protocol::COMMAND_HANDSHAKE:
           {
             const uint8_t *decode = event->payload;
             size_t decode_remain = event->payload_len;

             m_session_id = decode_i64(&decode, &decode_remain);
             if (m_session_id == 0)
             HT_THROW(Error::PROTOCOL_ERROR, "Bad session id: 0");
             handler = new RequestHandlerHandshake(m_comm, m_master.get(),
                                                   m_session_id, event);

          }
          break;
      case Protocol::COMMAND_OPEN:
        handler = new RequestHandlerOpen(m_comm, m_master.get(),
                                         m_session_id, event);
        break;
      case Protocol::COMMAND_CLOSE:
        handler = new RequestHandlerClose(m_comm, m_master.get(),
                                          m_session_id, event);
        break;
      case Protocol::COMMAND_MKDIR:
        handler = new RequestHandlerMkdir(m_comm, m_master.get(),
                                          m_session_id, event);
        break;
      case Protocol::COMMAND_DELETE:
        handler = new RequestHandlerDelete(m_comm, m_master.get(),
                                           m_session_id, event);
        break;
      case Protocol::COMMAND_ATTRSET:
        handler = new RequestHandlerAttrSet(m_comm, m_master.get(),
                                            m_session_id, event);
        break;
      case Protocol::COMMAND_ATTRGET:
        handler = new RequestHandlerAttrGet(m_comm, m_master.get(),
                                            m_session_id, event);
        break;
      case Protocol::COMMAND_ATTRINCR:
        handler = new RequestHandlerAttrIncr(m_comm, m_master.get(),
                                             m_session_id, event);
        break;
      case Protocol::COMMAND_ATTREXISTS:
        handler = new RequestHandlerAttrExists(m_comm, m_master.get(),
                                               m_session_id, event);
        break;
      case Protocol::COMMAND_ATTRLIST:
        handler = new RequestHandlerAttrList(m_comm, m_master.get(),
                                             m_session_id, event);
        break;
      case Protocol::COMMAND_ATTRDEL:
        handler = new RequestHandlerAttrDel(m_comm, m_master.get(),
                                            m_session_id, event);
        break;
      case Protocol::COMMAND_EXISTS:
        handler = new RequestHandlerExists(m_comm, m_master.get(),
                                           m_session_id, event);
        break;
      case Protocol::COMMAND_READDIR:
        handler = new RequestHandlerReaddir(m_comm, m_master.get(),
                                            m_session_id, event);
        break;
      case Protocol::COMMAND_READDIRATTR:
        handler = new RequestHandlerReaddirAttr(m_comm, m_master.get(),
                                                m_session_id, event);
        break;
      case Protocol::COMMAND_READPATHATTR:
        handler = new RequestHandlerReadpathAttr(m_comm, m_master.get(),
                                                 m_session_id, event);
        break;
      case Protocol::COMMAND_LOCK:
        handler = new RequestHandlerLock(m_comm, m_master.get(),
                                         m_session_id, event);
        break;
      case Protocol::COMMAND_RELEASE:
        handler = new RequestHandlerRelease(m_comm, m_master.get(),
                                            m_session_id, event);
        break;
      case Protocol::COMMAND_STATUS:
        handler = new RequestHandlerStatus(m_comm, m_master.get(), event);
        break;
      case Protocol::COMMAND_SHUTDOWN:
        handler = new RequestHandlerShutdown(m_comm, m_master.get(),
                                             m_session_id, event);
        break;
      default:
        HT_THROWF(Error::PROTOCOL_ERROR, "Unimplemented command (%llu)",
                  (Llu)event->header.command);
      }
      m_app_queue->add(handler);
    }
    catch (Exception &e) {
      ResponseCallback cb(m_comm, event);
      HT_ERROR_OUT << e << HT_END;
      String errmsg = format("%s - %s", e.what(), Error::get_text(e.code()));
      cb.error(Error::PROTOCOL_ERROR, errmsg);
    }
  }
  else if (event->type == Hypertable::Event::CONNECTION_ESTABLISHED) {
    HT_INFOF("%s", event->to_str().c_str());
  }
  else if (event->type == Hypertable::Event::DISCONNECT) {
    m_app_queue->add( new RequestHandlerDestroySession(m_master.get(), m_session_id) );
    cout << flush;
  }
  else if (event->type == Hypertable::Event::TIMER) {
    int error;
    try {
      m_app_queue->add(new Hyperspace::RequestHandlerDoMaintenance(m_master.get(), event) );
    }
    catch (Exception &e) {
      HT_ERROR_OUT << e << HT_END;
    }

    if ((error = m_comm->set_timer(m_maintenance_interval->get(), shared_from_this())) != Error::OK)
       HT_FATALF("Problem setting timer - %s", Error::get_text(error));

  }

  else {
    HT_INFOF("%s", event->to_str().c_str());
  }

}
