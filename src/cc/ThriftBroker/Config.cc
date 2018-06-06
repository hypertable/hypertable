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
#include "Thrift.h"

namespace Hypertable { namespace Config {

void init_thrift_client_options() {
  cmdline_desc().add_options()
    ("thrift-broker", str()->default_value("localhost:15867"),
        "Thrift client endpoint in <host:port> format")
    ("thrift-timeout", i32(),
        "Timeout in milli-seconds for thrift client connections")
	("thrift-transport", str()->default_value("framed"),
		"Thrift transport")
    ;

  alias("thrift-transport", "ThriftBroker.Transport");
  alias("thrift-timeout", "ThriftBroker.Timeout");
  // hidden aliases
  alias("thrift-host", "ThriftBroker.Host");
  alias("thrift-port", "ThriftBroker.Port");
}

void init_thrift_client() {
  // prepare hidden aliases to be synced
  Endpoint e = InetAddr::parse_endpoint(get_str("thrift-broker"));
  bool defaulted = properties->defaulted("thrift-broker");
  properties->set("thrift-host", e.host, defaulted);
  properties->set("thrift-port", e.port, !e.port || defaulted);
  // redirect thrift output
  redirect_thrift_output();
}

void init_thrift_broker_options() {
  cmdline_desc().add_options()
    ("port", i16()->default_value(15867), "Listening port")
    ("pidfile", str(), "File to contain the process id")
    ("log-api", boo()->default_value(false), "Enable or disable API logging")
    ("workers", i32()->default_value(50), "Worker threads")
	("thrift-transport", str()->default_value("framed"), "Thrift transport")
    ;

  alias("thrift-transport", "ThriftBroker.Transport");
  alias("port", "ThriftBroker.Port");
  alias("log-api", "ThriftBroker.API.Logging");
  alias("workers", "ThriftBroker.Workers");
  // hidden aliases
  alias("thrift-timeout", "ThriftBroker.Timeout");
}

void init_thrift_broker() {
  init_generic_server();
  // redirect thrift output
  redirect_thrift_output();
}

static void thrift_output_handler(const char *message) {
#if !defined(HT_DISABLE_LOG_ALL) && !defined(HT_DISABLE_LOG_ERROR)
  if (message && Logger::get()->is_enabled(Logger::Priority::ERROR))
    Logger::get()->log(Logger::Priority::ERROR, message);
#endif
}

void redirect_thrift_output() {
  apache::thrift::GlobalOutput.setOutputFunction(thrift_output_handler);
}

}} // namespace Hypertable::Config
