/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

#include "Common/Compat.h"
#include "Common/Logger.h"
#include "Common/Init.h"
#include "Common/Thread.h"

#include <set>

#include "DfsBroker/Lib/Client.h"

#include "Hypertable/Lib/Config.h"

#include "Hypertable/Master/Context.h"
#include "Hypertable/Master/MetaLogDefinitionMaster.h"
#include "Hypertable/Master/OperationProcessor.h"
#include "Hypertable/Master/ReferenceManager.h"
#include "Hypertable/Master/ResponseManager.h"

#include "OperationTest.h"

using namespace Hypertable;
using namespace Config;

namespace {

  typedef Meta::list<GenericServerPolicy, DfsClientPolicy,
                     HyperspaceClientPolicy, DefaultCommPolicy> Policies;

  void verify_order_test1(std::set<String> &seen, const String &name) {
    if (name == "A" || name == "B")
      HT_ASSERT(seen.count("E") == 1);
    else if (name == "C" || name == "D")
      HT_ASSERT(seen.count("F") == 1);
    else if (name == "E") {
      HT_ASSERT(seen.count("H") == 1);
      HT_ASSERT(seen.count("G") == 1);
    }
    else if (name == "F") {
      HT_ASSERT(seen.count("G") == 1);
      HT_ASSERT(seen.count("M") == 1);
    }
    else if (name == "G") {
      HT_ASSERT(seen.count("H") == 1);
      HT_ASSERT(seen.count("I") == 1);
    }
    else if (name == "H") {
      HT_ASSERT(seen.count("J") == 1);
      HT_ASSERT(seen.count("K") == 1);
      HT_ASSERT(seen.count("L") == 1);
    }
    else if (name == "I") {
      HT_ASSERT(seen.count("K") == 1);
      HT_ASSERT(seen.count("L") == 1);
      HT_ASSERT(seen.count("M") == 1);
    }
  }

  void verify_order_test2(std::set<String> &seen, const String &name) {
    if (name == "A") {
      HT_ASSERT(seen.count("B") == 1);
      HT_ASSERT(seen.count("C") == 1);
      HT_ASSERT(seen.count("D") == 1);
      HT_ASSERT(seen.count("E") == 1);
      HT_ASSERT(seen.count("I") == 1);
    }
    else if (name == "B") {
      HT_ASSERT(seen.count("C") == 1);
      HT_ASSERT(seen.count("D") == 1);
      HT_ASSERT(seen.count("E") == 1);
      HT_ASSERT(seen.count("I") == 1);
    }
    else if (name == "C") {
      HT_ASSERT(seen.count("D") == 1);
      HT_ASSERT(seen.count("E") == 1);
      HT_ASSERT(seen.count("I") == 1);
      HT_ASSERT(seen.count("H") == 1);
    }
    else if (name == "D") {
      HT_ASSERT(seen.count("E") == 1);
      HT_ASSERT(seen.count("I") == 1);
    }
    else if (name == "E")
      HT_ASSERT(seen.count("I") == 1);
    else if (name == "F")
      HT_ASSERT(seen.count("H") == 1);
    else if (name == "G")
      HT_ASSERT(seen.count("H") == 1);
    else if (name == "I")
      HT_ASSERT(seen.count("J") == 1);
    else if (name == "H")
      HT_ASSERT(seen.count("J") == 1);
  }

} // local namespace


int main(int argc, char **argv) {

  try {
    init_with_policies<Policies>(argc, argv);
    DependencySet dependencies, exclusivities, obstructions;
    std::vector<OperationPtr> operations;
    ContextPtr context = new Context();
    OperationPtr operation;
    std::vector<String> results;
    std::set<String> seen;
    String str;
    std::vector<MetaLog::EntityPtr> entities;

    context->comm = Comm::instance();
    context->conn_manager = new ConnectionManager(context->comm);
    context->props = properties;
    context->dfs = new DfsBroker::Client(context->conn_manager, context->props);
    context->toplevel_dir = properties->get_str("Hypertable.Directory");
    String log_dir = context->toplevel_dir + "/servers/master/log";
    boost::trim_if(context->toplevel_dir, boost::is_any_of("/"));
    context->toplevel_dir = String("/") + context->toplevel_dir;
    context->mml_definition = new MetaLog::DefinitionMaster(context, "master");
    context->mml_writer = new MetaLog::Writer(context->dfs, context->mml_definition,
                                              log_dir + "/" + context->mml_definition->name(),
                                              entities);

    ResponseManagerContext *rmctx = new ResponseManagerContext(context->mml_writer);
    context->response_manager = new ResponseManager(rmctx);
    Thread response_manager_thread(*context->response_manager);

    context->reference_manager = new ReferenceManager();

    context->op = new OperationProcessor(context, 4);

    /**
     *  TEST 1
     */

    dependencies.insert("op1");
    exclusivities.clear();
    operation = new OperationTest(context, results, "A", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    operation = new OperationTest(context, results, "B", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("op2");
    exclusivities.clear();
    operation = new OperationTest(context, results, "C", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    operation = new OperationTest(context, results, "D", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("op3");
    dependencies.insert("op4");
    exclusivities.clear();
    exclusivities.insert("op1");
    operation = new OperationTest(context, results, "E", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("op3");
    dependencies.insert("rs2");
    exclusivities.clear();
    exclusivities.insert("op2");
    operation = new OperationTest(context, results, "F", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("op4");
    dependencies.insert("op5");
    exclusivities.clear();
    exclusivities.insert("op3");
    operation = new OperationTest(context, results, "G", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("/n1");
    dependencies.insert("/n2");
    dependencies.insert("rs1");
    exclusivities.clear();
    exclusivities.insert("op4");
    operation = new OperationTest(context, results, "H", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("/n2");
    dependencies.insert("rs1");
    dependencies.insert("rs2");
    exclusivities.clear();
    exclusivities.insert("op5");
    operation = new OperationTest(context, results, "I", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    exclusivities.clear();
    exclusivities.insert("/n1");
    operation = new OperationTest(context, results, "J", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    exclusivities.clear();
    exclusivities.insert("/n2");
    operation = new OperationTest(context, results, "K", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    exclusivities.clear();
    exclusivities.insert("rs1");
    operation = new OperationTest(context, results, "L", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    exclusivities.clear();
    exclusivities.insert("rs2");
    operation = new OperationTest(context, results, "M", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    context->op->add_operations(operations);

    context->op->wait_for_empty();

    for (size_t i=0; i<results.size(); i++) {
      if (!boost::ends_with(results[i], "[0]")) {
        str = results[i] + "[0]";
        HT_ASSERT(seen.count(str) == 1);
        verify_order_test1(seen, results[i]);
      }
      seen.insert(results[i]);
    }

    /**
     *  TEST 2
     */
    results.clear();
    operations.clear();
    exclusivities.clear();
    dependencies.clear();
    obstructions.clear();

    dependencies.insert("foo");
    operation = new OperationTest(context, results, "A", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();

    exclusivities.insert("foo");
    operation = new OperationTest(context, results, "E", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    operation = new OperationTest(context, results, "D", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.insert("rs1");
    operation = new OperationTest(context, results, "C", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();
    
    operation = new OperationTest(context, results, "B", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    exclusivities.clear();

    dependencies.insert("rs1");
    operation = new OperationTest(context, results, "F", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    operation = new OperationTest(context, results, "G", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();

    dependencies.insert("wow");
    obstructions.insert("rs1");
    operation = new OperationTest(context, results, "H", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();
    obstructions.clear();

    dependencies.insert("wow");
    obstructions.insert("foo");
    operation = new OperationTest(context, results, "I", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();
    obstructions.clear();

    obstructions.insert("wow");
    dependencies.insert("unknown");
    operation = new OperationTest(context, results, "J", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    context->op->add_operations(operations);

    context->op->wait_for_empty();

    seen.clear();
    for (size_t i=0; i<results.size(); i++) {
      if (!boost::ends_with(results[i], "[0]")) {
        str = results[i] + "[0]";
        HT_ASSERT(seen.count(str) == 1);
        verify_order_test2(seen, results[i]);
      }
      seen.insert(results[i]);
    }

    /**
     *  TEST 3 (test blocked state)
     */

    OperationTestPtr operation_foo = new OperationTest(context, results, "foo", OperationState::STARTED);
    OperationTestPtr operation_bar = new OperationTest(context, results, "bar", OperationState::STARTED);

    operations.clear();
    operation_foo->block();
    operations.push_back(operation_foo);
    operation_bar->block();
    operations.push_back(operation_bar);
    context->op->add_operations(operations);
    context->op->wait_for_idle();

    HT_ASSERT(context->op->size() == 2);

    operation_foo->unblock();
    context->op->wake_up();
    poll(0, 0, 1000);
    context->op->wait_for_idle();
    
    HT_ASSERT(context->op->size() == 1);

    operation_bar->unblock();
    context->op->wake_up();
    context->op->wait_for_empty();

    HT_ASSERT(context->op->empty());

    /**
     *  TEST 4 (perpetual)
     */
    dependencies.clear();
    exclusivities.clear();
    obstructions.clear();
    obstructions.insert("yabadabadoo");
    OperationTest *operation_perp = new OperationTest(context, results, "perp", dependencies, exclusivities, obstructions);
    operation_perp->set_is_perpetual(true);
    obstructions.clear();

    operation = operation_perp;
    context->op->add_operation(operation);
    context->op->wait_for_idle();

    dependencies.insert("yabadabadoo");
    operation_foo = new OperationTest(context, results, "foo", dependencies, exclusivities, obstructions);
    operation_bar = new OperationTest(context, results, "bar", dependencies, exclusivities, obstructions);

    operations.clear();
    operations.push_back(operation_foo);
    operations.push_back(operation_bar);
    context->op->add_operations(operations);
    context->op->wait_for_idle();

    context->op->shutdown();
    context->op->join();

    context->response_manager->shutdown();
    response_manager_thread.join();
    delete rmctx;
    delete context->response_manager;
    delete context->reference_manager;

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    _exit(1);
  }
  return 0;
}
