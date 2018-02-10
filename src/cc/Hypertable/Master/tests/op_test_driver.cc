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

#include <Hypertable/Master/BalancePlanAuthority.h>
#include <Hypertable/Master/Context.h>
#include <Hypertable/Master/LoadBalancer.h>
#include <Hypertable/Master/MetaLogDefinitionMaster.h>
#include <Hypertable/Master/OperationCreateNamespace.h>
#include <Hypertable/Master/OperationCreateTable.h>
#include <Hypertable/Master/OperationDropNamespace.h>
#include <Hypertable/Master/OperationDropTable.h>
#include <Hypertable/Master/OperationInitialize.h>
#include <Hypertable/Master/OperationMoveRange.h>
#include <Hypertable/Master/OperationProcessor.h>
#include <Hypertable/Master/OperationRecreateIndexTables.h>
#include <Hypertable/Master/OperationRenameTable.h>
#include <Hypertable/Master/OperationSystemUpgrade.h>
#include <Hypertable/Master/OperationToggleTableMaintenance.h>
#include <Hypertable/Master/RecoveredServers.h>
#include <Hypertable/Master/RangeServerConnectionManager.h>
#include <Hypertable/Master/ReferenceManager.h>
#include <Hypertable/Master/ResponseManager.h>

#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/MetaLogReader.h>
#include <Hypertable/Lib/QualifiedRangeSpec.h>
#include <Hypertable/Lib/RangeSpec.h>
#include <Hypertable/Lib/RangeState.h>
#include <Hypertable/Lib/TableIdentifier.h>
#include <Hypertable/Lib/TableParts.h>

#include <FsBroker/Lib/Client.h>

#include <Common/FailureInducer.h>
#include <Common/Init.h>
#include <Common/System.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/ConnectionHandlerFactory.h>
#include <AsyncComm/ReactorFactory.h>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <thread>
#include <vector>

using namespace Hypertable;
using namespace Config;
using namespace Hyperspace;
using namespace std;

namespace {

  String g_mml_dir;
  uint16_t g_rs_port = 0;

  struct AppPolicy : Config::Policy {
    static void init_options() {
      cmdline_desc("Usage: %s <test>\n\n"
                   "  This program tests failures and state transitions\n"
                   "  of various Master operations.  Valid tests include:\n"
                   "\n"
                   "  initialize\n"
                   "  system_upgrade\n"
                   "  create_namespace\n"
                   "  drop_namespace\n"
                   "  create_table\n"
                   "  create_table_with_index\n"
                   "  drop_table\n"
                   "  rename_table\n"
                   "  move_range\n"
                   "  balance_plan_authority\n"
                   "  toggle_table_maintenance\n"
                   "  recreate_index_tables\n"
                   "\nOptions");
      cmdline_hidden_desc().add_options()("test", str(), "test to run");
      cmdline_positional_desc().add("test", -1);
    }
    static void init() {
      if (!has("test")) {
        HT_ERROR_OUT <<"test name required\n"<< cmdline_desc() << HT_END;
        exit(EXIT_FAILURE);
      }
    }
  };

  typedef Meta::list<GenericServerPolicy, FsClientPolicy,
                     HyperspaceClientPolicy, DefaultCommPolicy, AppPolicy> Policies;

  vector<RangeServerConnectionPtr> g_rsc;

  void initialize_test_with_servers(ContextPtr &context, size_t server_count,
                                    std::vector<MetaLog::EntityPtr> &entities) {

    HT_ASSERT(server_count <= 5);

    RangeServerConnectionPtr rsc;
    size_t server_index = 0;

    while (true) {

      rsc = make_shared<RangeServerConnection>("rs1", "rs1.hypertable.com", InetAddr("72.14.204.99", g_rs_port));
      context->rsc_manager->connect_server(rsc, "rs1.hypertable.com", InetAddr("72.14.204.99", 33567), InetAddr("72.14.204.99", g_rs_port));
      context->add_available_server("rs1");
      g_rsc.push_back(rsc);
      entities.push_back(rsc);

      if (++server_index == server_count)
        break;

      rsc = make_shared<RangeServerConnection>("rs2", "rs2.hypertable.com", InetAddr("69.147.125.65", g_rs_port));
      context->rsc_manager->connect_server(rsc, "rs2.hypertable.com", InetAddr("69.147.125.65", 30569), InetAddr("69.147.125.65", g_rs_port));
      context->add_available_server("rs2");
      g_rsc.push_back(rsc);
      entities.push_back(rsc);

      if (++server_index == server_count)
        break;

      rsc = make_shared<RangeServerConnection>("rs3", "rs3.hypertable.com", InetAddr("72.14.204.98", g_rs_port));
      context->rsc_manager->connect_server(rsc, "rs3.hypertable.com", InetAddr("72.14.204.98", 33572), InetAddr("72.14.204.98", g_rs_port));
      context->add_available_server("rs3");
      g_rsc.push_back(rsc);
      entities.push_back(rsc);

      if (++server_index == server_count)
        break;

      rsc = make_shared<RangeServerConnection>("rs4", "rs4.hypertable.com", InetAddr("69.147.125.62", g_rs_port));
      context->rsc_manager->connect_server(rsc, "rs4.hypertable.com", InetAddr("69.147.125.62", 30569), InetAddr("69.147.125.62", g_rs_port));
      context->add_available_server("rs4");
      g_rsc.push_back(rsc);
      entities.push_back(rsc);

      if (++server_index == server_count)
        break;

      rsc = make_shared<RangeServerConnection>("rs5", "rs5.hypertable.com", InetAddr("70.147.125.62", g_rs_port));
      context->rsc_manager->connect_server(rsc, "rs5.hypertable.com", InetAddr("70.147.125.62", 30569), InetAddr("70.147.125.62", g_rs_port));
      context->add_available_server("rs5");
      g_rsc.push_back(rsc);
      entities.push_back(rsc);

      break;
    }

    context->mml_writer =
      make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                   g_mml_dir, entities);
    
  }

  void initialize_test(ContextPtr &context,
                       std::vector<MetaLog::EntityPtr> &entities) {
    OperationPtr operation;

    context->op = std::make_unique<OperationProcessor>(context, 4);
    context->mml_writer =
      make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                   g_mml_dir, entities);
    for (auto &entity : entities) {
      operation = dynamic_pointer_cast<Operation>(entity);
      if (operation) {
	if (operation->get_remove_approval_mask()) {
          context->reference_manager->add(operation);
          if (dynamic_cast<OperationMoveRange *>(entity.get()))
            context->add_move_operation(operation);
        }
        context->op->add_operation(operation);
      }
    }
  }

  typedef std::multimap<String, int32_t> ExpectedResultsMap;

  void run_test2(ContextPtr &context, std::vector<MetaLog::EntityPtr> &entities,
                 const String &failure_point, ofstream &out) {
    std::vector<OperationPtr> operations;
    std::vector<MetaLog::EntityPtr> tmp_entities;

    context->op = std::make_unique<OperationProcessor>(context, 4);

    FailureInducer::instance->clear();
    if (failure_point != "")
      FailureInducer::instance->parse_option(failure_point);

    for (auto &entity : entities) {
      OperationPtr operation {dynamic_pointer_cast<Operation>(entity)};
      if (operation) {
        if (operation->get_remove_approval_mask()) {
          context->reference_manager->add(operation);
          if (dynamic_cast<OperationMoveRange *>(entity.get()))
            context->add_move_operation(operation);
        }
        operations.push_back(operation);
        HT_INFOF("Adding operation %s id=%lld state=%s",
                 operation->label().c_str(), (Lld)operation->id(),
                 OperationState::get_text(operation->get_state()));
      }
    }
    context->op->add_operations(operations);

    if (failure_point == "")
      context->op->wait_for_empty();
    else
      context->op->join();

    context->mml_writer = 0;
    MetaLog::ReaderPtr mml_reader =
      make_shared<MetaLog::Reader>(context->dfs, context->mml_definition, g_mml_dir);
    entities.clear();
    mml_reader->get_entities(entities);

    // Remove the BalancePlanAuthority object
    for (auto &entity : entities) {
      if (dynamic_cast<BalancePlanAuthority *>(entity.get()) == 0)
        tmp_entities.push_back(entity);
    }
    entities.swap(tmp_entities);

    DependencySet strs;
    string label = format("[%s] ", failure_point.c_str());
    for (auto &entity : entities) {
      OperationPtr operation {dynamic_pointer_cast<Operation>(entity)};
      RangeServerConnectionPtr rsc {dynamic_pointer_cast<RangeServerConnection>(entity)};
      if (operation) {
        out << label << operation->name() << " {" << OperationState::get_text(operation->get_state()) << "}\n";
        operation->dependencies(strs);
        for (auto &str : strs)
          out << label << "  dependency: " << str << "\n";
        operation->obstructions(strs);
        for (auto &str : strs)
          out << label << "  obstruction: " << str << "\n";
        operation->exclusivities(strs);
        for (auto &str : strs)
          out << label << "  exclusivity: " << str << "\n";
      }
      else if (rsc)
        out << label << rsc->name() << " {" << rsc->location() << "}\n";
    }
    out << endl;

    context->reference_manager->clear();

    context->mml_writer =
      make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                   g_mml_dir, entities);
  }

  void create_table(ContextPtr &context,
                    std::vector<MetaLog::EntityPtr> &entities,
                    const string &table_name, const string &schema,
                    string &table_id) {
    ofstream out;
#if defined(__WIN32__) || defined(_WIN32)
    string devnull("nul");
#else
    string devnull("/dev/null");
#endif
    out.open(devnull);

    entities.push_back(make_shared<OperationCreateTable>(context, table_name, schema,
							 TableParts(TableParts::ALL)));

    run_test2(context, entities, "", out);

    out.close();

    std::vector<MetaLog::EntityPtr> entities2;

    // Remove operations
    for (auto &entity : entities) {
      if (dynamic_cast<RangeServerConnection *>(entity.get()))
        entities2.push_back(entity);
    }
    entities.swap(entities2);

    if (!context->namemap->name_to_id(table_name, table_id))
      HT_FATALF("Unable to determine table ID for table \"%s\"",
                table_name.c_str());

    context->mml_writer.reset();

    initialize_test(context, entities);
    this_thread::sleep_for(chrono::milliseconds(100));
  }

  bool check_for_diff(const string &basename) {
    string cmd;
    int ret;

    // Transform table ID to "<tid>" since they can change depending on the
    // order in which the tests are run

    cmd = format("mv %s.output %s.tmp", basename.c_str(), basename.c_str());
    if ((ret = system(cmd.c_str())) != 0) {
      cout << "Shell command \"" << cmd << "\" returned " << ret << endl;
      return false;
    }

    cmd = format("sed 's/[0-9/]* move range/<tid> move range/g' %s.tmp > %s.output",
                 basename.c_str(), basename.c_str());
    if ((ret = system(cmd.c_str())) != 0) {
      cout << "Shell command \"" << cmd << "\" returned " << ret << endl;
      return false;
    }

    cmd = format("rm %s.tmp", basename.c_str());
    if ((ret = system(cmd.c_str())) != 0) {
      cout << "Shell command \"" << cmd << "\" returned " << ret << endl;
      return false;
    }

    cmd = format("diff %s.output %s.golden", basename.c_str(), basename.c_str());
    if ((ret = system(cmd.c_str())) != 0) {
      cout << "Shell command \"" << cmd << "\" returned " << ret << endl;
      return false;
    }

    return true;
  }


} // local namespace

void create_namespace_test(ContextPtr &context);
void drop_namespace_test(ContextPtr &context);
void create_table_test(ContextPtr &context);
void drop_table_test(ContextPtr &context);
void create_table_with_index_test(ContextPtr &context);
void rename_table_test(ContextPtr &context);
void master_initialize_test(ContextPtr &context);
void system_upgrade_test(ContextPtr &context);
void move_range_test(ContextPtr &context);
void balance_plan_authority_test(ContextPtr &context);
void toggle_table_maintenance_test(ContextPtr &context);
void recreate_index_tables_test(ContextPtr &context);


int main(int argc, char **argv) {
  ContextPtr context;
  std::vector<MetaLog::EntityPtr> entities;

  // Register ourselves as the Comm-layer proxy master
  ReactorFactory::proxy_master = true;

  try {
    init_with_policies<Policies>(argc, argv);

    g_rs_port = get_i16("Hypertable.RangeServer.Port");

    // Default Hyperspace replicat host
    std::vector<String> replicas;
    replicas.push_back("localhost");
    properties->set("Hyperspace.Replica.Host", replicas);

    Hyperspace::SessionPtr hyperspace = make_shared<Hyperspace::Session>(Comm::instance(), properties);

    context = make_shared<Context>(properties, hyperspace);
    context->test_mode = true;

    boost::trim_if(context->toplevel_dir, boost::is_any_of("/"));
    context->toplevel_dir = String("/") + context->toplevel_dir;
    context->monitoring = make_shared<Monitoring>(context.get());

    context->mml_definition = make_shared<MetaLog::DefinitionMaster>(context, "master");
    g_mml_dir = context->toplevel_dir + "/servers/master/log/" + context->mml_definition->name();
    context->mml_writer =
      make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                   g_mml_dir, entities);

    MetaLog::EntityPtr entity;
    context->get_balance_plan_authority(entity);
    BalancePlanAuthorityPtr bpa = static_pointer_cast<BalancePlanAuthority>(entity);

    context->recovered_servers = make_shared<RecoveredServers>();

    FailureInducer::instance = new FailureInducer();
    context->request_timeout = 600;

    context->balancer = new LoadBalancer(context);

    context->response_manager->set_mml_writer(context->mml_writer);

    String testname = get_str("test");

    if (testname == "initialize")
      master_initialize_test(context);
    else if (testname == "system_upgrade")
      system_upgrade_test(context);
    else if (testname == "create_namespace")
      create_namespace_test(context);
    else if (testname == "drop_namespace")
      drop_namespace_test(context);
    else if (testname == "create_table")
      create_table_test(context);
    else if (testname == "drop_table")
      drop_table_test(context);
    else if (testname == "create_table_with_index")
      create_table_with_index_test(context);
    else if (testname == "rename_table")
      rename_table_test(context);
    else if (testname == "move_range")
      move_range_test(context);
    else if (testname == "balance_plan_authority")
      balance_plan_authority_test(context);
    else if (testname == "toggle_table_maintenance")
      toggle_table_maintenance_test(context);
    else if (testname == "recreate_index_tables")
      recreate_index_tables_test(context);
    else {
      HT_ERRORF("Unrecognized test name: %s", testname.c_str());
      quick_exit(EXIT_FAILURE);
    }

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    quick_exit(EXIT_FAILURE);
  }

  return 0;
}


void create_namespace_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  context->mml_writer =
    make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                 g_mml_dir, entities);

  entities.push_back(make_shared<OperationCreateNamespace>(context, "foo", 0) );

  ofstream out("create_namespace.output", ios::out|ios::trunc);

  run_test2(context, entities, "create-namespace-INITIAL:throw:0", out);
  run_test2(context, entities, "create-namespace-ASSIGN_ID-a:throw:0", out);
  run_test2(context, entities, "create-namespace-ASSIGN_ID-b:throw:0", out);
  run_test2(context, entities, "", out);

  out.close();

  context->op->shutdown();
  context->op->join();

  if (!check_for_diff("create_namespace"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}


void drop_namespace_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  context->mml_writer =
    make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                 g_mml_dir, entities);

  entities.push_back(make_shared<OperationDropNamespace>(context, "foo", 0) );

  ofstream out("drop_namespace.output", ios::out|ios::trunc);

  run_test2(context, entities, "drop-namespace-INITIAL:throw:0", out);
  run_test2(context, entities, "drop-namespace-STARTED-a:throw:0", out);
  run_test2(context, entities, "drop-namespace-STARTED-b:throw:0", out);
  run_test2(context, entities, "", out);

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("drop_namespace"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}

namespace {
  const char *schema_str = "<Schema>\n"
    "  <AccessGroup name=\"default\">\n"
    "    <ColumnFamily>\n"
    "      <Name>column</Name>\n"
    "    </ColumnFamily>\n"
    "  </AccessGroup>\n"
    "</Schema>";

  const char *index_schema_str = "<Schema>\n"
    "  <AccessGroup name=\"default\">\n"
    "    <ColumnFamily>\n"
    "      <Name>a</Name>\n"
    "      <Index>true</Index>\n"
    "    </ColumnFamily>\n"
    "    <ColumnFamily>\n"
    "      <Name>b</Name>\n"
    "      <QualifierIndex>true</QualifierIndex>\n"
    "    </ColumnFamily>\n"
    "  </AccessGroup>\n"
    "</Schema>";
}



void create_table_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  initialize_test_with_servers(context, 2, entities);

  ofstream out("create_table.output", ios::out|ios::trunc);

  MetaLog::EntityPtr entity = make_shared<OperationCreateTable>(context, "tablefoo", schema_str, 
								TableParts(TableParts::ALL));
  dynamic_pointer_cast<Operation>(entity)->add_exclusivity("/tablefoo");

  entities.push_back(entity);

  run_test2(context, entities, "create-table-INITIAL:throw:0", out);
  run_test2(context, entities, "Utility-create-table-in-hyperspace-1:throw:0", out);
  run_test2(context, entities, "Utility-create-table-in-hyperspace-2:throw:0", out);
  run_test2(context, entities, "create-table-ASSIGN_ID:throw:0", out);
  run_test2(context, entities, "create-table-WRITE_METADATA-a:throw:0", out);
  run_test2(context, entities, "create-table-WRITE_METADATA-b:throw:0", out);
  run_test2(context, entities, "create-table-ASSIGN_LOCATION:throw:0", out);
  run_test2(context, entities, "create-table-LOAD_RANGE-a:throw:0", out);
  run_test2(context, entities, "create-table-LOAD_RANGE-b:throw:0", out);
  run_test2(context, entities, "create-table-ACKNOWLEDGE:throw:0", out);
  run_test2(context, entities, "", out);

  context->rsc_manager->disconnect_server(g_rsc[0]);
  initialize_test(context, entities);
  this_thread::sleep_for(chrono::milliseconds(100));
  context->rsc_manager->connect_server(g_rsc[0], "rs1.hypertable.com", InetAddr("localhost", 30267),
                                       InetAddr("localhost", g_rs_port));
  context->op->wait_for_empty();

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("create_table"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}


void drop_table_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  initialize_test_with_servers(context, 2, entities);

  string table_id;
  create_table(context, entities, "drop_table_test", index_schema_str, table_id);

  ofstream out("drop_table.output", ios::out|ios::trunc);

  OperationPtr operation = 
    make_shared<OperationDropTable>(context, "drop_table_test", true,
				    TableParts(TableParts::ALL));
  entities.push_back(operation);

  run_test2(context, entities, "drop-table-INITIAL:throw:0", out);
  run_test2(context, entities, "drop-table-DROP_VALUE_INDEX-1:throw:0", out);
  run_test2(context, entities, "drop-table-DROP_VALUE_INDEX-2:throw:0", out);
  run_test2(context, entities, "drop-table-DROP_QUALIFIER_INDEX-1:throw:0", out);
  run_test2(context, entities, "drop-table-DROP_QUALIFIER_INDEX-2:throw:0", out);
  run_test2(context, entities, "drop-table-SCAN_METADATA:throw:0", out);
  run_test2(context, entities, "drop-table-ISSUE_REQUESTS:throw:0", out);
  run_test2(context, entities, "", out);

  context->op->wait_for_empty();

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("drop_table"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}


void create_table_with_index_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  initialize_test_with_servers(context, 2, entities);

  ofstream out("create_table_with_index.output", ios::out|ios::trunc);

  MetaLog::EntityPtr entity =
    make_shared<OperationCreateTable>(context, "tablefoo_index",
				      index_schema_str, TableParts(TableParts::ALL));
  dynamic_pointer_cast<Operation>(entity)->add_exclusivity("/tablefoo_index");

  entities.push_back(entity);

  run_test2(context, entities, "create-table-INITIAL:throw:0", out);
  run_test2(context, entities, "create-table-CREATE_INDEX-1:throw:0", out);
  run_test2(context, entities, "create-table-CREATE_INDEX-2:throw:0", out);
  run_test2(context, entities, "create-table-CREATE_QUALIFIER_INDEX-1:throw:0", out);
  run_test2(context, entities, "create-table-CREATE_QUALIFIER_INDEX-2:throw:0", out);
  run_test2(context, entities, "create-table-FINALIZE:throw:0", out);
  run_test2(context, entities, "create-table-FINALIZE:throw:1", out);
  run_test2(context, entities, "", out);

  context->rsc_manager->disconnect_server(g_rsc[0]);
  initialize_test(context, entities);
  this_thread::sleep_for(chrono::milliseconds(100));
  context->rsc_manager->connect_server(g_rsc[0], "rs1.hypertable.com", InetAddr("localhost", 30267),
                          InetAddr("localhost", g_rs_port));
  context->op->wait_for_empty();

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("create_table_with_index"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}


void rename_table_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  context->mml_writer =
    make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                 g_mml_dir, entities);

  entities.push_back(make_shared<OperationRenameTable>(context, "tablefoo", "tablebar") );

  ofstream out("rename_table.output", ios::out|ios::trunc);

  run_test2(context, entities, "rename-table-STARTED:throw:0", out);
  run_test2(context, entities, "", out);

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("rename_table"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}


void master_initialize_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  initialize_test_with_servers(context, 4, entities);

  ofstream out("master_initialize.output", ios::out|ios::trunc);

  entities.push_back( make_shared<OperationInitialize>(context) );

  run_test2(context, entities, "initialize-INITIAL:throw:0", out);
  run_test2(context, entities, "initialize-STARTED:throw:0", out);
  run_test2(context, entities, "initialize-ASSIGN_METADATA_RANGES:throw:0", out);
  run_test2(context, entities, "initialize-LOAD_ROOT_METADATA_RANGE:throw:0", out);
  run_test2(context, entities, "initialize-LOAD_SECOND_METADATA_RANGE:throw:0", out);
  run_test2(context, entities, "initialize-WRITE_METADATA:throw:0", out);
  run_test2(context, entities, "initialize-CREATE_RS_METRICS:throw:0", out);
  run_test2(context, entities, "", out);

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("master_initialize"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}


namespace {
  const char *old_schema_str = \
    "<Schema generation=\"1\">\n"
    "  <AccessGroup name=\"default\">\n"
    "    <ColumnFamily id=\"1\">\n"
    "      <Generation>1</Generation>\n"
    "      <Name>LogDir</Name>\n"
    "      <Counter>false</Counter>\n"
    "      <deleted>false</deleted>\n"
    "    </ColumnFamily>\n"
    "  </AccessGroup>\n"
    "</Schema>\n";
}



void system_upgrade_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  // Write "old" schema
  String tablefile = context->toplevel_dir + "/tables/0/0";
  uint64_t handle = context->hyperspace->open(tablefile, OPEN_FLAG_READ|OPEN_FLAG_WRITE);
  context->hyperspace->attr_set(handle, "schema", old_schema_str, strlen(old_schema_str));
  context->hyperspace->close(handle);

  context->mml_writer =
    make_shared<MetaLog::Writer>(context->dfs, context->mml_definition,
                                 g_mml_dir, entities);

  MetaLog::EntityPtr entity = make_shared<OperationSystemUpgrade>(context);
  entities.push_back(entity);

#if defined(__WIN32__) || defined(_WIN32)
  string devnull("nul");
#else
  string devnull("/dev/null");
#endif
  ofstream out(devnull);

  run_test2(context, entities, "", out);

  context->op->shutdown();
  context->op->join();

  out.close();

  HT_ASSERT(dynamic_pointer_cast<Operation>(entity)->get_state() == OperationState::COMPLETE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}


void move_range_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  initialize_test_with_servers(context, 4, entities);

  TableIdentifier table;
  RangeSpec range;
  String transfer_log = "/hypertable/servers/log/xferlog";
  uint64_t soft_limit = 100000000;

  table.id = "3/6/3";
  table.generation = 2;
  range.start_row = "bar";
  range.end_row = "foo";

  MetaLog::EntityPtr entity;
  context->get_balance_plan_authority(entity);
  entities.push_back(entity);

  entity = make_shared<OperationMoveRange>(context, "rs1", 0, table, range,
					   transfer_log, soft_limit, true);
  entities.push_back(entity);

  ofstream out("move_range.output", ios::out|ios::trunc);

  run_test2(context, entities, "move-range-INITIAL-b:throw:0", out);

  String initial_location =
    dynamic_pointer_cast<OperationMoveRange>(entity)->get_location();

  run_test2(context, entities, "", out);

  String final_location =
    dynamic_pointer_cast<OperationMoveRange>(entity)->get_location();

  HT_ASSERT(initial_location == final_location);

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("move_range"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}

void fill_ranges(vector<QualifiedRangeSpec> &root_specs,
                 vector<RangeState> &root_states,
                 vector<QualifiedRangeSpec> &metadata_specs,
                 vector<RangeState> &metadata_states,
                 vector<QualifiedRangeSpec> &system_specs,
                 vector<RangeState> &system_states,
                 vector<QualifiedRangeSpec> &user_specs,
                 vector<RangeState> &user_states)
{
  QualifiedRangeSpec spec;

  root_specs.clear();
  root_states.clear();
  spec = QualifiedRangeSpec(TableIdentifier("0/0"), RangeSpec("start_root0", "end_root0"));
  root_specs.push_back(spec);
  root_states.push_back(RangeState());
  spec = QualifiedRangeSpec(TableIdentifier("0/0"), RangeSpec("start_root1", "end_root1"));
  root_specs.push_back(spec);
  root_states.push_back(RangeState());

  metadata_specs.clear();
  metadata_states.clear();
  spec = QualifiedRangeSpec(TableIdentifier("1/0"), RangeSpec("start_meta1", "end_meta1"));
  metadata_specs.push_back(spec);
  metadata_states.push_back(RangeState());
  spec = QualifiedRangeSpec(TableIdentifier("1/0"), RangeSpec("start_meta2", "end_meta2"));
  metadata_specs.push_back(spec);
  metadata_states.push_back(RangeState());

  system_specs.clear();
  system_states.clear();

  user_specs.clear();
  user_states.clear();
  spec = QualifiedRangeSpec(TableIdentifier("2/0"), RangeSpec("start_user1", "end_user1"));
  user_specs.push_back(spec);
  user_states.push_back(RangeState());
  spec = QualifiedRangeSpec(TableIdentifier("2/0"), RangeSpec("start_user2", "end_user2"));
  user_specs.push_back(spec);
  user_states.push_back(RangeState());
  spec = QualifiedRangeSpec(TableIdentifier("2/0"), RangeSpec("start_user3", "end_user3"));
  user_specs.push_back(spec);
  user_states.push_back(RangeState());
}

void balance_plan_authority_test(ContextPtr &context) {
  std::ofstream out("balance_plan_authority_test.output");
  std::vector<MetaLog::EntityPtr> entities;

  initialize_test_with_servers(context, 5, entities);

  BalancePlanAuthority *bpa = context->get_balance_plan_authority();

  vector<QualifiedRangeSpec> root_specs;
  vector<RangeState> root_states;
  vector<QualifiedRangeSpec> metadata_specs;
  vector<RangeState> metadata_states;
  vector<QualifiedRangeSpec> system_specs;
  vector<RangeState> system_states;
  vector<QualifiedRangeSpec> user_specs;
  vector<RangeState> user_states;

  // populate ranges
  fill_ranges(root_specs, root_states, metadata_specs, metadata_states,
              system_specs, system_states, user_specs, user_states);
  g_rsc[0]->set_removed();
  context->remove_available_server("rs1");
  bpa->create_recovery_plan("rs1", root_specs, root_states, metadata_specs, metadata_states,
          system_specs, system_states, user_specs, user_states);
  out << "Recovered rsc1: " << *bpa << std::endl << std::endl;

  fill_ranges(root_specs, root_states, metadata_specs, metadata_states,
              system_specs, system_states, user_specs, user_states);
  g_rsc[1]->set_removed();
  context->remove_available_server("rs2");
  bpa->create_recovery_plan("rs2", root_specs, root_states, metadata_specs, metadata_states,
          system_specs, system_states, user_specs, user_states);
  out << "Recovered rsc2: " << *bpa << std::endl << std::endl;

  fill_ranges(root_specs, root_states, metadata_specs, metadata_states,
              system_specs, system_states, user_specs, user_states);
  g_rsc[2]->set_removed();
  context->remove_available_server("rs3");
  bpa->create_recovery_plan("rs3", root_specs, root_states, metadata_specs, metadata_states,
          system_specs, system_states, user_specs, user_states);
  out << "Recovered rsc3: " << *bpa << std::endl << std::endl;

  fill_ranges(root_specs, root_states, metadata_specs, metadata_states,
              system_specs, system_states, user_specs, user_states);
  g_rsc[3]->set_removed();
  context->remove_available_server("rs4");
  bpa->create_recovery_plan("rs4", root_specs, root_states, metadata_specs, metadata_states,
          system_specs, system_states, user_specs, user_states);
  out << "Recovered rsc4: " << *bpa << std::endl;
  out.flush();

  // remove the timestamp, then compare against the golden file
  String cmd = "cat balance_plan_authority_test.output | perl -e 'while (<>) { s/timestamp=.*?201\\d,/timestamp=0,/g; print; }' > output; diff output balance_plan_authority_test.golden";
  if (system(cmd.c_str()) != 0) {
    std::cout << "balance_plan_authority_test.output differs from golden file\n";
    quick_exit(EXIT_FAILURE);
  }

  context = 0;
  quick_exit(EXIT_SUCCESS);
}

void toggle_table_maintenance_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  initialize_test_with_servers(context, 2, entities);

  string table_id;
  create_table(context, entities, "toggle", index_schema_str, table_id);

  ofstream out("toggle_table_maintenance.output", ios::out|ios::trunc);

  MetaLog::EntityPtr entity =
    make_shared<OperationToggleTableMaintenance>(context, "toggle", TableMaintenance::OFF);
  entities.push_back(entity);

  context->add_available_server(g_rsc[0]->location());
  context->add_available_server(g_rsc[1]->location());

  run_test2(context, entities, "toggle-table-maintenance-INITIAL:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-UPDATE_HYPERSPACE-1:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-UPDATE_HYPERSPACE-2:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-SCAN_METADATA:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-ISSUE_REQUESTS:throw:0", out);
  run_test2(context, entities, "", out);

  // Check for "maintenance_disabled" attribute
  try {
    String tablefile = context->toplevel_dir + "/tables/" + table_id;
    DynamicBuffer dbuf;
    context->hyperspace->attr_get(tablefile, "maintenance_disabled", dbuf);
  }
  catch (Exception &e) {
    HT_FATAL_OUT << e << HT_END;
  }

  context->mml_writer->record_removal(entity);

  entities.clear();
  entities.push_back(g_rsc[0]);
  entities.push_back(g_rsc[1]);

  entity = make_shared<OperationToggleTableMaintenance>(context, "toggle", TableMaintenance::ON);
  entities.push_back(entity);

  run_test2(context, entities, "toggle-table-maintenance-INITIAL:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-UPDATE_HYPERSPACE-1:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-UPDATE_HYPERSPACE-2:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-SCAN_METADATA:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-ISSUE_REQUESTS:throw:0", out);
  run_test2(context, entities, "", out);

  // Check for absence of "maintenance_disabled" attribute
  try {
    String tablefile = context->toplevel_dir + "/tables/" + table_id;
    DynamicBuffer dbuf;
    context->hyperspace->attr_get(tablefile, "maintenance_disabled", dbuf);
    HT_FATAL("Attribute \"maintenance_disabled\" found when it should not exist");
  }
  catch (Exception &e) {
  }

  out.close();

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("toggle_table_maintenance"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}


void recreate_index_tables_test(ContextPtr &context) {
  std::vector<MetaLog::EntityPtr> entities;

  initialize_test_with_servers(context, 2, entities);

  string table_id;
  create_table(context, entities, "recreate_index_tables", index_schema_str, table_id);

  ofstream out("recreate_index_tables.output", ios::out|ios::trunc);

  OperationPtr operation = make_shared<OperationRecreateIndexTables>(context, "recreate_index_tables",
								     TableParts(TableParts::ALL));
  entities.push_back(operation);

  context->add_available_server(g_rsc[0]->location());
  context->add_available_server(g_rsc[1]->location());

  run_test2(context, entities, "recreate-index-tables-INITIAL:throw:0", out);
  run_test2(context, entities, "toggle-table-maintenance-SCAN_METADATA:throw:0", out);
  run_test2(context, entities, "drop-table-INITIAL:throw:0", out);
  run_test2(context, entities, "create-table-ASSIGN_ID:throw:0", out);
  run_test2(context, entities, "recreate-index-tables-RESUME_TABLE_MAINTENANCE-a:throw:0", out);
  run_test2(context, entities, "recreate-index-tables-RESUME_TABLE_MAINTENANCE-b:throw:0", out);
  run_test2(context, entities, "", out);

  // Check for absence of "maintenance_disabled" attribute
  try {
    String tablefile = context->toplevel_dir + "/tables/" + table_id;
    DynamicBuffer dbuf;
    context->hyperspace->attr_get(tablefile, "maintenance_disabled", dbuf);
    HT_FATAL("Attribute \"maintenance_disabled\" found when it should not exist");
  }
  catch (Exception &e) {
  }

  out.close();

  context->op->shutdown();
  context->op->join();

  out.close();

  if (!check_for_diff("recreate_index_tables"))
    quick_exit(EXIT_FAILURE);

  context = 0;
  quick_exit(EXIT_SUCCESS);
}
