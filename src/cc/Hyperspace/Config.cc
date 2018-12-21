/** -*- C++ -*-
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License.
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
#include "Common/InetAddr.h"
#include "Config.h"

#include <algorithm>
#include <iostream>
#include <vector>

#include <strings.h>

namespace Hypertable { namespace Config {

void init_hyperspace_client_options() {
  cmdline_desc().add_options()
    ("hyperspace", str(),
        "hyperspace master server to connect to in <host:port> format")
    ("hyperspace-timeout", i32(30000),
        "Timeout in milliseconds for hyperspace connection")
    ("keepalive", g_i32(10000),
        "Interval in milliseconds of keepalive message from Hyperspace client")
    ("lease-interval", g_i32(60000),
        "Hyperspace master lease interval in milliseconds")
    ("grace-period", g_i32(60000),
        "Grace period in milliseconds before 'jeopardy' for hyperspace client")
    ;
  alias("hyperspace-timeout", "Hyperspace.Timeout");
  alias("keepalive", "Hyperspace.KeepAlive.Interval");
  alias("lease-interval", "Hyperspace.Lease.Interval");
  alias("grace-period", "Hyperspace.GracePeriod");
  // hidden aliases
  alias("hs-host", "Hyperspace.Replica.Host");
  alias("hs-port", "Hyperspace.Replica.Port");
}

void init_hyperspace_client() {
  // prepare hidden aliases to be synced
  if (properties->has("hyperspace")) {
    Endpoint e = InetAddr::parse_endpoint(get_str("hyperspace"));
    bool defaulted = properties->defaulted("hyperspace");
    Strings hosts;
    if (properties->has("Hyperspace.Replica.Host"))
      hosts = properties->get<gStrings>("Hyperspace.Replica.Host");

    size_t i;
    for (i=0; i<hosts.size(); i++) {
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__WINDOWS_386__)
      if (stricmp(hosts[i].c_str(), e.host.c_str()) == 0)
        break;
#else
      if (strcasecmp(hosts[i].c_str(), e.host.c_str()) == 0)
        break;
#endif
    }
    // if not found ...
    if (i == hosts.size()) {
      hosts.insert(hosts.begin(), e.host);
      properties->set("hs-host", hosts, defaulted);
    }
    properties->set("hs-port", e.port, !e.port || defaulted);
  }
}

void init_hyperspace_command_shell_options() {
  CommandShell::add_options(cmdline_desc());
}

void init_hyperspace_master_options() {
  cmdline_desc().add_options()
    ("port", i16(), 
     "Hyperspace master listening port")
    ("dir", str("hyperspace"), 
     "Hyperspace root directory name")
    ("keepalive", g_i32(10000), 
     "Interval in milliseconds of keepalive message from Hyperspace client")
    ("lease-interval", g_i32(20000),
     "Hyperspace master lease interval in milliseconds")
    ;
  alias("reactors", "Hyperspace.Replica.Reactors");
  alias("workers", "Hyperspace.Replica.Workers");
  alias("port", "Hyperspace.Replica.Port");
  alias("dir", "Hyperspace.Replica.Dir");
  alias("keepalive", "Hyperspace.KeepAlive.Interval");
  alias("lease-interval", "Hyperspace.Lease.Interval");
}

}} // namespace Hypertable::Config
