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

#include "GcWorker.h"

#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/Namespace.h>

#include <FsBroker/Lib/Client.h>

#include <Common/Error.h>
#include <Common/Init.h>
#include <Common/Logger.h>
#include <Common/System.h>
#include <Common/Thread.h>

#include <iostream>

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  struct MyPolicy : Policy {
    static void init_options() {
      cmdline_desc().add_options()
        ("dryrun,n", "Dryrun, don't modify (delete files etc.)")
        ("full", "Do a full scan of DFS files and compare with METADATA.")
        ;
    }
  };

  typedef Cons<MyPolicy, DefaultCommPolicy> AppPolicy;

} // local namespace

int
main(int ac, char *av[]) {
  NamespacePtr ns;
  ContextPtr context;

  try {
    init_with_policy<AppPolicy>(ac, av);

    context->comm = Comm::instance();
    context->conn_manager = make_shared<ConnectionManager>(context->comm);
    context->props = properties;
    context->toplevel_dir = properties->get_str("Hypertable.Directory");
    boost::trim_if(context->toplevel_dir, boost::is_any_of("/"));
    context->toplevel_dir = String("/") + context->toplevel_dir;
    context->dfs = std::make_shared<FsBroker::Lib::Client>(context->conn_manager, context->props);

    ClientPtr client = make_shared<Hypertable::Client>("htgc");
    ns = client->open_namespace("sys");
    context->metadata_table = ns->open_table("METADATA");

    GcWorker worker(context);

    worker.gc();

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    quick_exit(EXIT_FAILURE);
  }
  quick_exit(EXIT_SUCCESS);
}
