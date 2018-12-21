/* -*- C++ -*-
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
#include "Config.h"

namespace Hypertable { namespace Config {

void init_range_server_options() {
  cmdline_desc().add_options()
    ("log-broker", str(), "Use the specified FS broker (in <host>:<port> "
        "format for commit log)")
    ("crash-test", str(), "Arguments for crash simulation")
    ;
    alias("port", "Hypertable.RangeServer.Port");
    alias("log-host", "Hypertable.RangeServer.CommitLog.FsBroker.Host");
    alias("log-port", "Hypertable.RangeServer.CommitLog.FsBroker.Port");
    alias("reactors", "Hypertable.RangeServer.Reactors");
    alias("workers", "Hypertable.RangeServer.Workers");
}

void init_range_server() {
  String logbroker = get("log-broker", String());

  if (logbroker.length()) {
    Endpoint e = InetAddr::parse_endpoint(logbroker);

    if (!e.port)
      HT_THROWF(Error::CONFIG_BAD_ARGUMENT, "expected <host>:<port> format for "
          "log-broker, got '%s'", logbroker.c_str());

    properties->set("log-host", e.host);
    properties->set("log-port", e.port);
  }
}

}} // namespace Hypertable::Config
