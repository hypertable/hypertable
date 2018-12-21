/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
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

#include <Common/Compat.h>

#include "Shutdown.h"

#include <Hypertable/RangeServer/RangeServer.h>

#include <AsyncComm/ResponseCallback.h>

#include <Common/Config.h>

#include <chrono>
#include <thread>

using namespace Hypertable;
using namespace Hypertable::RangeServer::Request::Handler;

void Shutdown::run() {
  ResponseCallback cb(m_comm, m_event);
  try {
    m_range_server->shutdown();
  }
  catch (Exception &e) {
    HT_ERROR_OUT << "Shutdown " << e << HT_END;
  }
  cb.response_ok();
  this_thread::sleep_for(chrono::milliseconds(2000));
  HT_INFO("Exiting RangeServer.");
  if (Config::has("pidfile"))
    FileUtils::unlink(Config::get_str("pidfile"));
  quick_exit(EXIT_SUCCESS);
}
