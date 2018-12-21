/* -*- c++ -*-
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

#ifndef HYPERTABLE_THRIFTBROKER_CONFIG_H
#define HYPERTABLE_THRIFTBROKER_CONFIG_H

#include "AsyncComm/Config.h"
#include "AsyncComm/ConfigHandler.h"

namespace Hypertable { namespace Config {

  // init helpers
  void init_thrift_client_options();
  void init_thrift_client();
  void init_thrift_broker_options();
  void init_thrift_broker();
  void redirect_thrift_output();

  struct ThriftClientPolicy : Policy {
    static void init_options() { init_thrift_client_options(); }
    static void init() { init_thrift_client(); }
  };

  /**
   * Mutually exclusive with GenericServerPolicy
   */
  struct ThriftBrokerPolicy : Policy {
    static void init_options() { init_thrift_broker_options(); }
    static void init() { init_thrift_broker(); }
  };

}} // namespace Hypertable::Config

#endif // HYPERTABLE_THRIFTBROKER_CONFIG_H
