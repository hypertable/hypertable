/*
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

#include <Common/Compat.h>

#include <Hypertable/Master/BalanceAlgorithmLoad.h>

#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/BalancePlan.h>

#include <Hyperspace/Session.h>

#include <Common/Init.h>
#include <Common/Error.h>
#include <Common/System.h>

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>

extern "C" {
#include <poll.h>
#include <stdio.h>
}

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace std;
using namespace boost;

namespace {

  const char *usage =
    "\n"
    "Usage: ht_balance_plan_generator [options] <rs_metrics_file>\n\n"
    "Description:\n"
    "  This program is used to generate a load balancing plan.\n"
    "  The <rs_metrics_file> argument indicates the location of the file containing the dump\n"
    "  of the 'sys/RS_METRICS' table.\n\n"
    "Options";

  struct AppPolicy : Config::Policy {
    static void init_options() {
      allow_unregistered_options(true);
      cmdline_desc(usage).add_options()
        ("help,h", "Show this help message and exit")
        ("help-config", "Show help message for config properties")
        ("namespace", str()->default_value("/"),
         "Namespace in which to create the dummy RS_METRICS table if needed")
        ("table", str()->default_value("DUMMY_RS_METRICS"),
         "Name of the table in which to load the rs_metrics_file")
        ("rs-metrics-loaded",  boo()->zero_tokens()->default_value(false),
         "If true then assume RS_METRICS is already loaded in namespace/table")
        ("load-balancer", str()->default_value("basic-distribute-load"),
         "Type of load balancer to be used.")
        ("verbose,v", boo()->zero_tokens()->default_value(false),
         "Show more verbose output")
        ("balance-plan-file,b",  str()->default_value(""),
         "File in which to dump balance plan.")
        ("rs-metrics-dump", str(), "File containing dump of 'sys/RS_METRICS' table.");
    }
  };
  bool verbose=false;
}


typedef Meta::list<AppPolicy, DefaultCommPolicy> Policies;

void generate_balance_plan(PropertiesPtr &props, const String &load_balancer,
    ContextPtr &context, BalancePlanPtr &plan);
void create_table(String &ns, String &tablename, String &rs_metrics_file);

int main(int argc, char **argv) {
  String ns_str, table_str, load_balancer, rs_metrics_file;
  String balance_plan_file;
  System::initialize(System::locate_install_dir(argv[0]));

  try {
    init_with_policies<Policies>(argc, argv);

    if (has("verbose")) {
      verbose = get_bool("verbose");
    }

    table_str = get_str("table");
    ns_str = get_str("namespace");
    balance_plan_file = get_str("balance-plan-file");
    load_balancer = get_str("load-balancer");
    bool loaded = get_bool("rs-metrics-loaded");
    if (has("rs-metrics-dump")) {
      if (!loaded) {
        rs_metrics_file = get_str("rs-metrics-dump");
        create_table(ns_str, table_str, rs_metrics_file);
      }
    }
    else if (!loaded) {
      ns_str = "sys";
      table_str = "RS_METRICS";
    }

    ClientPtr client = std::make_shared<Hypertable::Client>(System::install_dir);
    NamespacePtr ns = client->open_namespace(ns_str);
    TablePtr rs_metrics = ns->open_table(table_str);
    BalancePlanPtr plan = std::make_shared<BalancePlan>();
    ContextPtr context = std::make_shared<Context>(properties);
    context->rsc_manager.reset();
    context->rs_metrics_table = rs_metrics;
    generate_balance_plan(context->props, load_balancer, context, plan);
    ostream *oo;

    if (balance_plan_file.size() == 0)
      oo = &cout;
    else
      oo = new ofstream(balance_plan_file.c_str(), ios_base::app);

    size_t ii;

    *oo << "BalancePlan: { ";
    if (plan->moves.size()>0) {
      for(ii=0; ii<plan->moves.size()-1; ++ii) {
        *oo << "(" << plan->moves[ii]->table.id << "[" << plan->moves[ii]->range.start_row
          << ".." << plan->moves[ii]->range.end_row << "], " << plan->moves[ii]->source_location
          << ", " << plan->moves[ii]->dest_location << "), ";
      }

      *oo << "(" << plan->moves[ii]->table.id << "[" << plan->moves[ii]->range.start_row
        << ".." << plan->moves[ii]->range.end_row << "], " << plan->moves[ii]->source_location
        << ", " << plan->moves[ii]->dest_location << ")";
    }
    *oo << " }" << endl;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    exit(EXIT_FAILURE);
  }

  fflush(stdout);
  quick_exit(EXIT_SUCCESS); // don't bother with static objects
}

void generate_balance_plan(PropertiesPtr &props, const String &load_balancer,
    ContextPtr &context, BalancePlanPtr &plan) {

  if (load_balancer != "basic-distribute-load")
    HT_THROW(Error::NOT_IMPLEMENTED,
             (String)"Only 'basic-distribute-load' balancer is supported. '" + load_balancer
             + "' balancer not supported.");

  std::vector<RangeServerStatistics> range_server_stats;
  // TODO fill this vector; otherwise disk usage is not taken into account

  BalanceAlgorithmLoad balancer(context, range_server_stats);
  std::vector<RangeServerConnectionPtr> balanced;
  balancer.compute_plan(plan, balanced);
}


void create_table(String &ns, String &table, String &rs_metrics_file) {

  String hql = (String) " USE \'" + ns + "\';\n" +
                " DROP TABLE IF EXISTS " + table + ";\n" +
                " CREATE TABLE " + table + "(\n" +
                "   server MAX_VERSIONS=336,\n" +
                "   range MAX_VERSIONS=24,\n" +
                "   \'range_start_row\' MAX_VERSIONS=1,\n" +
                "   \'range_move\' MAX_VERSIONS=1,\n" +
                "   ACCESS GROUP server (server),\n" +
                "   ACCESS GROUP range (range, \'range_start_row\', \'range_move\')\n" +
                " );\n" +
                " LOAD DATA INFILE \'" + rs_metrics_file + "\' INTO TABLE " + table + ";";
;
  // load from file
  String install_dir = System::install_dir;
  String cmd_str = install_dir + (String)"/bin/ht_hypertable --test-mode --exec \""+ hql + "\"";
  if (verbose)
    HT_INFOF("Running command: %s", cmd_str.c_str());
  HT_EXPECT(::system(cmd_str.c_str()) == 0, Error::FAILED_EXPECTATION);
}
