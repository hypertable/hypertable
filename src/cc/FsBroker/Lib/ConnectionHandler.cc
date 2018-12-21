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

/// @file
/// Definitions for ConnectionHandler.
/// This file contains definitions for ConnectionHandler, a class that handles
/// incoming client requests.

#include <Common/Compat.h>

#include "ConnectionHandler.h"
#include "Client.h"
#include "Request/Handler/Factory.h"

#include <AsyncComm/ApplicationQueue.h>

#include <Common/Config.h>
#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/String.h>
#include <Common/StringExt.h>
#include <Common/Serialization.h>

using namespace Hypertable;
using namespace Hypertable::FsBroker::Lib;

void ConnectionHandler::handle(EventPtr &event) {

  if (event->type == Event::MESSAGE) {

    //event->display()

    try {
      if (event->header.command ==
          Request::Handler::Factory::FUNCTION_SHUTDOWN) {
        uint16_t flags = 0;
        ResponseCallback cb(m_comm, event);
        if (event->payload_len >= 2) {
          const uint8_t *ptr = event->payload;
          size_t remain = event->payload_len;
          flags = Serialization::decode_i16(&ptr, &remain);
        }
        if ((flags & Client::SHUTDOWN_FLAG_IMMEDIATE) != 0)
          m_app_queue->shutdown();
        m_broker->shutdown(&cb);
        if (Config::has("pidfile"))
          FileUtils::unlink(Config::get_str("pidfile"));
        quick_exit(EXIT_SUCCESS);
      }
      m_app_queue->add(Request::Handler::Factory::create(m_comm, m_broker.get(),
                                                         event));
    }
    catch (Exception &e) {
      ResponseCallback cb(m_comm, event);
      HT_ERROR_OUT << e << HT_END;
      String errmsg = format("%s - %s", e.what(), Error::get_text(e.code()));
      cb.error(Error::PROTOCOL_ERROR, errmsg);
    }
  }
  else if (event->type == Event::DISCONNECT) {
    HT_DEBUGF("%s : Closing all open handles from %s", event->to_str().c_str(),
              event->addr.format().c_str());
    OpenFileMap &ofmap = m_broker->get_open_file_map();
    ofmap.remove_all(event->addr);
  }
  else {
    HT_DEBUGF("%s", event->to_str().c_str());
  }

}

