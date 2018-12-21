/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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
/// Definitions for LocationInitializer.
/// This file contains type definitions for LocationInitializer, a class used
/// to obtain the location string (<i>proxy name</i>) for the range server.

#include <Common/Compat.h>
#include "LocationInitializer.h"

#include <Hypertable/Lib/Master/Protocol.h>
#include <Hypertable/Lib/Master/Request/Parameters/RegisterServer.h>
#include <Hypertable/Lib/Master/Response/Parameters/RegisterServer.h>
#include <Hypertable/Lib/SystemVariable.h>

#include <Hyperspace/Session.h>

#include <Common/Config.h>
#include <Common/FileUtils.h>
#include <Common/Logger.h>
#include <Common/Serialization.h>
#include <Common/StatsSystem.h>
#include <Common/Path.h>
#include <Common/md5.h>
#include <Common/ScopeGuard.h>

#include <boost/algorithm/string.hpp>

using namespace Hypertable;
using namespace Hypertable::Lib;
using namespace Serialization;
using namespace std;

LocationInitializer::LocationInitializer(std::shared_ptr<RangeServerContext> &context)
  : m_context(context) {

  Path data_dir = m_context->props->get_str("Hypertable.DataDirectory");
  data_dir /= "/run";
  if (!FileUtils::exists(data_dir.string()))
    FileUtils::mkdirs(data_dir.string());
  m_location_file = (data_dir /= "/location").string();

  // Get location string
  {
    m_location = m_context->props->get_str("Hypertable.RangeServer.ProxyName");
    if (!m_location.empty()) {
      boost::trim(m_location);
      if (m_location == "*") {
        char location_hash[33];
        uint16_t port = m_context->props->get_i16("Hypertable.RangeServer.Port");
        md5_string(format("%s:%u", System::net_info().host_name.c_str(), port).c_str(), location_hash);
        m_location = format("rs-%s", String(location_hash).substr(0, 8).c_str());
      }
    }
    else if (FileUtils::exists(m_location_file)) {
      if (!FileUtils::read(m_location_file, m_location)) {
        HT_ERRORF("Problem reading location file '%s'", m_location_file.c_str());
        quick_exit(EXIT_FAILURE);
      }
      boost::trim(m_location);
      if (!m_location.empty())
        m_location_persisted = true;
    }
  }

}

bool LocationInitializer::is_removed(const String &path, Hyperspace::SessionPtr &hyperspace) {
  bool removed = false;
  if (!m_location.empty()) {
    String filename=path + "/" + m_location;
    uint64_t handle=0;
    HT_ON_SCOPE_EXIT(&Hyperspace::close_handle_ptr, hyperspace, &handle);
    uint32_t oflags = Hyperspace::OPEN_FLAG_READ;
    try {
      handle = hyperspace->open(filename, oflags);
      if (hyperspace->attr_exists(handle, "removed"))
        removed = true;
    }
    catch (Exception &e) {
      if (e.code() != Error::HYPERSPACE_FILE_NOT_FOUND)
        HT_FATAL_OUT << e << HT_END;
    }
  }
  return removed;
}

bool LocationInitializer::remove_location() {
  m_location.clear();
  return FileUtils::unlink(m_location_file);
}

CommBuf *LocationInitializer::create_initialization_request() {
  lock_guard<mutex> lock(m_mutex);
  StatsSystem stats;
  const char *base, *ptr;
  String datadirs = m_context->props->get_str("Hypertable.RangeServer.Monitoring.DataDirectories");
  uint16_t port = m_context->props->get_i16("Hypertable.RangeServer.Port");
  String dir;
  std::vector<String> dirs;

  base = datadirs.c_str();
  while ((ptr = strchr(base, ',')) != 0) {
    dir = String(base, ptr-base);
    boost::trim(dir);
    dirs.push_back(dir);
    base = ptr+1;
  }
  dir = String(base);
  boost::trim(dir);
  dirs.push_back(dir);

  stats.add_categories(StatsSystem::CPUINFO|StatsSystem::NETINFO|
                       StatsSystem::OSINFO|StatsSystem::PROCINFO, dirs);

  
  CommHeader header(Master::Protocol::COMMAND_REGISTER_SERVER);
  Master::Request::Parameters::RegisterServer params(m_location, port, m_lock_held,
                                                     stats);
  CommBuf *cbuf = new CommBuf(header, params.encoded_length());
  params.encode(cbuf->get_data_ptr_address());

  return cbuf;
}

bool LocationInitializer::process_initialization_response(Event *event) {
  int error = Protocol::response_code(event);

  if (error != Error::OK) {
    HT_ERROR_OUT << "Problem initializing Master connection - "
                 << Protocol::string_format_message(event) << HT_END;
    return false;
  }

  bool location_persisted = false;

  // Decode response
  const uint8_t *ptr = event->payload + 4;
  size_t remain = event->payload_len - 4;
  Master::Response::Parameters::RegisterServer params;
  params.decode(&ptr, &remain);

  // Update server state
  m_context->server_state->set(params.generation(), params.variables());

  {
    lock_guard<mutex> lock(m_mutex);
    if (m_location == "")
      m_location = params.location();
    else
      HT_ASSERT(m_location == params.location());
    location_persisted = m_location_persisted;
    m_handshake_complete = true;
  }

  if (!location_persisted) {
    if (FileUtils::write(m_location_file, params.location()) < 0) {
      HT_ERRORF("Unable to write location to file '%s'", m_location_file.c_str());
      quick_exit(EXIT_FAILURE);
    }
    {
      lock_guard<mutex> lock(m_mutex);
      m_location_persisted = true;
    }
  }
  m_cond.notify_all();
  return true;
}

String LocationInitializer::get() {
  lock_guard<mutex> lock(m_mutex);
  return m_location;
}

void LocationInitializer::wait_for_handshake() {
  unique_lock<mutex> lock(m_mutex);
  m_cond.wait(lock, [this](){ return m_handshake_complete; });
}
