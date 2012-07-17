/** -*- C++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
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
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

#include "Common/Compat.h"
#include <iostream>
#include <fstream>
#include <string>

extern "C" {
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>
}

#include "Common/FileUtils.h"
#include "Common/Init.h"
#include "Common/Usage.h"

#include "AsyncComm/ApplicationQueue.h"
#include "AsyncComm/Comm.h"

#include "DfsBroker/Lib/Config.h"
#include "DfsBroker/Lib/ConnectionHandlerFactory.h"

#include "CephBroker.h"

using namespace Hypertable;
using namespace Config;
using namespace std;

struct AppPolicy : Config::Policy {
  static void init_options() {
    cmdline_desc().add_options()
      ("ceph-version", "Show Ceph version and exit")
      ;
  }

  static void init() {
    alias("reactors", "DfsBroker.Ceph.Reactors");
    alias("workers", "DfsBroker.Ceph.Workers");
    alias("ceph_mon", "CephBroker.MonAddr");
    alias("port", "CephBroker.Port");

    if (has("ceph-version")) {
      cout <<"  Ceph: Can't determine version " << endl;
      _exit(0);
    }
  }
};

typedef Meta::list<AppPolicy, DfsBrokerPolicy, DefaultCommPolicy> Policies;

int main (int argc, char **argv) {
  //  HT_INFOF("ceph/main attempting to create pieces %d", argc);
  try {
    init_with_policies<Policies>(argc, argv);
    int port = get_i16("DfsBroker.Port");
    int worker_count = get_i32("CephBroker.Workers");

    if (has("CephBroker.Port"))
      port = get_i16("CephBroker.Port");

    Comm *comm = Comm::instance();
    ApplicationQueuePtr app_queue = new ApplicationQueue(worker_count);
    HT_INFOF("attemping to create new CephBroker with address %s", properties->get_str("CephBroker.MonAddr").c_str());
    BrokerPtr broker = new CephBroker(properties);
    HT_INFO("Created CephBroker!");
    ConnectionHandlerFactoryPtr chfp =
      new DfsBroker::ConnectionHandlerFactory(comm, app_queue, broker);
    InetAddr listen_addr(INADDR_ANY, port);

    comm->listen(listen_addr, chfp);
    app_queue->join();
  }
  catch(Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
  return 0;
}

