/* -*- c++ -*-
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

/** @file
 * Definitions for ClusterId.
 * This file contains definitions for ClusterId, a class that provides access
 * to the Cluster ID.
 */

#include <Common/Compat.h>
#include "ClusterId.h"

#include <Common/Config.h>
#include <Common/DynamicBuffer.h>
#include <Common/InetAddr.h>
#include <Common/ScopeGuard.h>
#include <Common/SystemInfo.h>
#include <Common/md5.h>

#include <chrono>
#include <thread>

using namespace Hyperspace;
using namespace Hypertable;
using namespace std;

uint64_t ClusterId::id;

ClusterId::ClusterId(Hyperspace::SessionPtr &hyperspace,
                     bool generate_if_not_found) {

  string toplevel_dir = Config::properties->get_str("Hypertable.Directory");
  uint64_t handle = 0;
  
  HT_ON_SCOPE_EXIT(&Hyperspace::close_handle_ptr, hyperspace, &handle);

  // open the "master" file in hyperspace
  handle = hyperspace->open(toplevel_dir + "/master",
                            OPEN_FLAG_READ|OPEN_FLAG_WRITE);

  size_t retry_count = 12;

  while (true) {
    try {
      // get the "cluster_id" attribute
      DynamicBuffer buf;
      hyperspace->attr_get(handle, "cluster_id", buf);
      string cluster_id((const char *)buf.base, buf.fill());
      id = (uint64_t)strtoull(cluster_id.c_str(), 0, 0);
      return;
    }
    catch (Exception &e) {
      if (e.code() == Error::HYPERSPACE_ATTR_NOT_FOUND) {
        if (generate_if_not_found)
          break;
        if (--retry_count) {
          HT_INFO("Problem reading cluster ID from Hyperspace, will retry in 10 seconds ...");
          this_thread::sleep_for(chrono::milliseconds(10000));
          continue;
        }
      }
      HT_THROW2(e.code(), e, "Problem reading cluster ID from Hyperspace");
    }
  }

  // Generate new cluster ID
  uint16_t port = Config::properties->get_i16("Hypertable.Master.Port");
  InetAddr addr(System::net_info().primary_addr, port);
  string tmp = addr.format() + Hypertable::format("%u", (unsigned)time(0));
  id = (uint64_t)md5_hash(tmp.c_str());
  tmp = Hypertable::format("%llu", (Llu)id);

  HT_INFOF("Newly generated cluster ID = %llu", (Llu)id);

  // Store newly generated cluster Id to Hyperspace
  try {
    hyperspace->attr_set(handle, "cluster_id", tmp.c_str(), tmp.length());
  }
  catch (Exception &e) {
    HT_FATALF("Problem writing newly generated cluster ID to Hyperspace - %s"
              "(%s)", Error::get_text(e.code()), e.what());
  }
}
