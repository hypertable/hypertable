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

#ifndef HYPERTABLE_LIB_CONFIG_H
#define HYPERTABLE_LIB_CONFIG_H

#include "Common/Config.h"
#include "AsyncComm/Config.h"
#include "AsyncComm/ConfigHandler.h"
#include "Hyperspace/Config.h"
#include "FsBroker/Lib/Config.h"

namespace Hypertable { namespace Config {

  // init helpers
  void init_master_client_options();
  void init_master_client();
  void init_range_server_client_options();
  void init_range_server_client();

  struct ClientPolicy : Policy {
    static void init_options() {
      alias("workers", "Hypertable.Client.Workers");
    }
  };

  struct MasterClientPolicy : Policy {
    static void init_options() { init_master_client_options(); }
    static void init() { init_master_client(); }
  };

  struct RangeServerClientPolicy : Policy {
    static void init_options() { init_range_server_client_options(); }
    static void init() { init_range_server_client(); }
  };

  typedef Meta::list<ClientPolicy, HyperspaceClientPolicy, MasterClientPolicy,
                     DefaultCommPolicy> ClientPolicies;

  typedef Join<ClientPolicies>::type DefaultClientPolicy;

}} // namespace Hypertable::Config

#endif // HYPERTABLE_LIB_CONFIG_H
