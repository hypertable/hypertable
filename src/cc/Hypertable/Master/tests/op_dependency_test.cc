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

#include "OperationTest.h"

#include <Hypertable/Master/Context.h>
#include <Hypertable/Master/MetaLogDefinitionMaster.h>
#include <Hypertable/Master/OperationProcessor.h>
#include <Hypertable/Master/ReferenceManager.h>
#include <Hypertable/Master/ResponseManager.h>

#include <Hypertable/Lib/Config.h>

#include <FsBroker/Lib/Client.h>

#include <Common/Logger.h>
#include <Common/Init.h>
#include <Common/Thread.h>

#include <chrono>
#include <iostream>
#include <set>
#include <thread>

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  typedef Meta::list<GenericServerPolicy, FsClientPolicy,
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
    ContextPtr context = make_shared<Context>(properties);
    OperationPtr operation;
    std::vector<String> results;
    std::set<String> seen;
    String str;
    std::vector<MetaLog::EntityPtr> entities;

    String log_dir = context->toplevel_dir + "/servers/master/log";
    boost::trim_if(context->toplevel_dir, boost::is_any_of("/"));
    context->toplevel_dir = String("/") + context->toplevel_dir;
    context->mml_definition = make_shared<MetaLog::DefinitionMaster>(context, "master");
    context->mml_writer =
      make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                   log_dir + "/" + context->mml_definition->name(),
                                   entities);

    context->response_manager->set_mml_writer(context->mml_writer);

    context->op = std::make_unique<OperationProcessor>(context, 4);

    /**
     *  TEST 1
     */

    dependencies.insert("op1");
    exclusivities.clear();
    operation = make_shared<OperationTest>(context, results, "A", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    operation = make_shared<OperationTest>(context, results, "B", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("op2");
    exclusivities.clear();
    operation = make_shared<OperationTest>(context, results, "C", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    operation = make_shared<OperationTest>(context, results, "D", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("op3");
    dependencies.insert("op4");
    exclusivities.clear();
    exclusivities.insert("op1");
    operation = make_shared<OperationTest>(context, results, "E", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("op3");
    dependencies.insert("rs2");
    exclusivities.clear();
    exclusivities.insert("op2");
    operation = make_shared<OperationTest>(context, results, "F", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("op4");
    dependencies.insert("op5");
    exclusivities.clear();
    exclusivities.insert("op3");
    operation = make_shared<OperationTest>(context, results, "G", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("/n1");
    dependencies.insert("/n2");
    dependencies.insert("rs1");
    exclusivities.clear();
    exclusivities.insert("op4");
    operation = make_shared<OperationTest>(context, results, "H", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    dependencies.insert("/n2");
    dependencies.insert("rs1");
    dependencies.insert("rs2");
    exclusivities.clear();
    exclusivities.insert("op5");
    operation = make_shared<OperationTest>(context, results, "I", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    exclusivities.clear();
    exclusivities.insert("/n1");
    operation = make_shared<OperationTest>(context, results, "J", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    exclusivities.clear();
    exclusivities.insert("/n2");
    operation = make_shared<OperationTest>(context, results, "K", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    exclusivities.clear();
    exclusivities.insert("rs1");
    operation = make_shared<OperationTest>(context, results, "L", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.clear();
    exclusivities.clear();
    exclusivities.insert("rs2");
    operation = make_shared<OperationTest>(context, results, "M", dependencies, exclusivities, obstructions);
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
    operation = make_shared<OperationTest>(context, results, "A", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();

    exclusivities.insert("foo");
    operation = make_shared<OperationTest>(context, results, "E", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    operation = make_shared<OperationTest>(context, results, "D", dependencies, exclusivities, obstructions);
    operations.push_back(operation);

    dependencies.insert("rs1");
    operation = make_shared<OperationTest>(context, results, "C", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();
    
    operation = make_shared<OperationTest>(context, results, "B", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    exclusivities.clear();

    dependencies.insert("rs1");
    operation = make_shared<OperationTest>(context, results, "F", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    operation = make_shared<OperationTest>(context, results, "G", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();

    dependencies.insert("wow");
    obstructions.insert("rs1");
    operation = make_shared<OperationTest>(context, results, "H", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();
    obstructions.clear();

    dependencies.insert("wow");
    obstructions.insert("foo");
    operation = make_shared<OperationTest>(context, results, "I", dependencies, exclusivities, obstructions);
    operations.push_back(operation);
    dependencies.clear();
    obstructions.clear();

    obstructions.insert("wow");
    dependencies.insert("unknown");
    operation = make_shared<OperationTest>(context, results, "J", dependencies, exclusivities, obstructions);
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

    OperationTestPtr operation_foo = make_shared<OperationTest>(context, results, "foo", OperationState::STARTED);
    OperationTestPtr operation_bar = make_shared<OperationTest>(context, results, "bar", OperationState::STARTED);

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
    this_thread::sleep_for(chrono::milliseconds(1000));
    context->op->wait_for_idle();
    
    HT_ASSERT(context->op->size() == 1);

    operation_bar->unblock();
    context->op->wake_up();
    context->op->wait_for_empty();

    HT_ASSERT(context->op->empty());

    /**
     *  TEST 4 (make sure blocked operations hold up their dependencies)
     */

    operation_foo = make_shared<OperationTest>(context, results, "foo", OperationState::STARTED);
    operation_bar = make_shared<OperationTest>(context, results, "bar", OperationState::STARTED);
    OperationTestPtr operation_baz = make_shared<OperationTest>(context, results, "baz", OperationState::STARTED);

    /*
     *  foo -> bar -> baz
     */

    operation_foo->add_dependency("bar");
    operation_bar->add_dependency("baz");
    operation_bar->add_obstruction("bar");
    operation_baz->add_obstruction("baz");

    operation = operation_baz;
    operation->block();
    context->op->add_operation(operation);

    operation = operation_bar;
    operation->block();
    context->op->add_operation(operation);

    operation = operation_foo;
    context->op->add_operation(operation);

    this_thread::sleep_for(chrono::milliseconds(2000));
    HT_ASSERT(context->op->size() == 3);

    context->op->unblock("baz");
    this_thread::sleep_for(chrono::milliseconds(2000));
    HT_ASSERT(context->op->size() == 2);

    context->op->unblock("bar");
    context->op->wait_for_empty();

    // again, this time unblock in forward direction

    operation_foo = make_shared<OperationTest>(context, results, "foo", OperationState::STARTED);
    operation_bar = make_shared<OperationTest>(context, results, "bar", OperationState::STARTED);
    operation_baz = make_shared<OperationTest>(context, results, "baz", OperationState::STARTED);

    operation_foo->add_dependency("bar");
    operation_bar->add_dependency("baz");
    operation_bar->add_obstruction("bar");
    operation_baz->add_obstruction("baz");

    operation = operation_baz;
    operation->block();
    context->op->add_operation(operation);

    operation = operation_bar;
    operation->block();
    context->op->add_operation(operation);

    operation = operation_foo;
    context->op->add_operation(operation);

    this_thread::sleep_for(chrono::milliseconds(2000));
    HT_ASSERT(context->op->size() == 3);

    context->op->unblock("bar");
    this_thread::sleep_for(chrono::milliseconds(2000));
    HT_ASSERT(context->op->size() == 3);

    context->op->unblock("baz");
    context->op->wait_for_empty();

    /**
     *  TEST 5 (perpetual)
     */
    dependencies.clear();
    exclusivities.clear();
    obstructions.clear();
    obstructions.insert("yabadabadoo");
    OperationTestPtr operation_perp =
      make_shared<OperationTest>(context, results, "perp", dependencies,
                                 exclusivities, obstructions);
    operation_perp->set_is_perpetual(true);
    obstructions.clear();

    context->op->add_operation(operation_perp);
    context->op->wait_for_idle();

    dependencies.insert("yabadabadoo");
    operation_foo = make_shared<OperationTest>(context, results, "foo", dependencies, exclusivities, obstructions);
    operation_bar = make_shared<OperationTest>(context, results, "bar", dependencies, exclusivities, obstructions);

    operations.clear();
    operations.push_back(operation_foo);
    operations.push_back(operation_bar);
    context->op->add_operations(operations);
    context->op->wait_for_idle();

    context->start_shutdown();
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    quick_exit(EXIT_FAILURE);
  }
  quick_exit(EXIT_SUCCESS);
}
