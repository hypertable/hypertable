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

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>

#include "Common/Error.h"
#include "Common/FileUtils.h"
#include "Common/InetAddr.h"
#include "Common/Logger.h"
#include "Common/Init.h"
#include "Common/System.h"
#include "Common/Usage.h"

#include "AsyncComm/Comm.h"
#include "AsyncComm/ConnectionManager.h"

#include "FsBroker/Lib/Config.h"
#include "FsBroker/Lib/Client.h"

#include "Hypertable/Lib/MetaLog.h"
#include "Hypertable/Lib/MetaLogReader.h"

#include "Hypertable/RangeServer/MetaLogEntityRange.h"
#include "Hypertable/RangeServer/MetaLogDefinitionRangeServer.h"

#include "Hypertable/Master/BalancePlanAuthority.h"
#include "Hypertable/Master/MetaLogDefinitionMaster.h"
#include "Hypertable/Master/OperationMoveRange.h"
#include "Hypertable/Master/RangeServerConnection.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <unordered_map>

using namespace Hypertable;
using namespace Config;
using namespace std;
using namespace boost;


namespace {

  const char *usage = R"(
Usage: ht metalog_tool [options] <path>

metalog_tool is a program that can be used to manipulate a
mata log.  There are two types of meta logs, the Master Meta
Log (MML) and the Range Server Meta Log (RSML).  The Master
Meta Log holds the persistent state of the Master, including
the state of all in-progress operations.  It can be found at
the following path in the brokered file system:

/hypertable/servers/master/log/mml

The Range Server Meta Log holds the persistent state of a
Range Server, including the state of all ranges managed by
the server.  The Range Server Meta Logs can be found at the
following paths in the brokered file system:

/hypertable/servers/*/log/rsml

The format of both the MML and RSMLs are the same.  Each log
is represented as a directory and contains numerically named
log files.  Log files with a high numeric name supersede
files with lower numeric names.  The log file consists of a
series of serialized "entities", where an entity is the
persistent state of some program object or concept.  Each
entity has a unique ID and the same entity can be serialized
in the log multiple times, with the last serialization
superseding all previous ones.  The entities for the MML are
different than the entities for the RSML, but all entities
derive from the same base class, which is why a single tool
can be used to manipulate both logs.

NOTE: This tool should never be run on a log in which the
      owner of the log (Master or Range Server) is running.

GENERAL OPTIONS

  To print a human-readable version of a meta log to the
  terminal, the --dump option may be supplied.  By itself,
  this option causes the most recent state of each entity in
  the log to be displayed.  The output with this option is
  the same as the default output of the 'metalog_dump'
  program.

  $ ht metalog_tool --dump /hypertable/servers/master/log/mml

  To dump the contents of a specific log file, within the
  meta log directory, you can supply a pathname to the
  file itself:

  $ ht metalog_tool --dump /hypertable/servers/master/log/mml/249

  To dump all entity records within a meta log file, supply
  the --all option.  This may display the same entity
  multiple times which gives you a view of the state
  transitions the entity has made.  For example:

  $ ht metalog_tool --dump --all /hypertable/servers/master/log/mml/249

  One of the most common uses of the metalog_tool is to
  remove entities from a meta log.  This can be achieved
  with the --purge and --select options.

  The --select option can be used to select the entities
  that you would like to remove.  It accepts either a comma-
  separated list of entity IDs, or a comma-separated list of
  entity types.  For example, let's say you would like to
  remove the entity that is display with the following line
  in the dump output (take note of the ID 1355140):

  {MetaLog::Entity RangeServerConnection header={type=131072,checksum=1052026697,id=1355140,timestamp=Tue Jul 22 18:55:31 2014,flags=0,length=36} payload={ rs149 (mrashad21.i) state=REMOVED|BALANCED }

  $ ht metalog_tool --select 1355140 --purge /hypertable/servers/master/log/mml
  log version: 4
  1 entities purged.
  0 entities translated.
  0 entities modified.

  The number of entities successfully purged is displayed in
  the output ("1 entities purged.").  Now let's say you want
  to remove all of the OperationRecover entities from the
  log.  To do that, you can supply the entity name as the
  argument to the --select option:

  $ ht metalog_tool --select OperationRecover --purge /hypertable/servers/master/log/mml
  log version: 4
  791 entities purged.
  0 entities translated.
  0 entities modified.

  The set of entity types supported by --select is listed
  below.

  BalancePlanAuthority
  OperationAlterTable
  OperationBalance
  OperationCreateNamespace
  OperationCreateTable
  OperationDropNamespace
  OperationDropTable
  OperationInitialize
  OperationMoveRange
  OperationRecover
  OperationRecoverRanges
  OperationRecoverServer
  OperationRenameTable
  OperationStop
  RangeServerConnection

The following sections describe somewhat esoteric options
used to manipulate specific entities.  These options were
added to to fix bad meta log entries caused by bugs
discovered during Hypertable development which have since
been fixed.

MML OPTIONS

  To remove, from the BalancePlanAuthority entity, a
  specific table from the target of any moves, use the
  --balance-plan-drop-table option with the table ID of the
  table you wish to remove.  This will remove all
  ServerReceiverPlan records that contain the specified
  table ID:

  $ ht metalog_tool --balance-plan-drop-table "3/7" /hypertable/servers/master/log/mml

  To increment the BalancePlanAuthority generation number,
  use the --balance-plan-incr-generation option:

  $ ht metalog_tool --balance-plan-incr-generation /hypertable/servers/master/log/mml

  To change the destination of ranges that exist in a
  recovery plan inside the BalancePlanAuthority entity, use
  the --balance-plan-change-destination option.  The
  argument to this option has the following format:

    <failed-server>,<plan-type>,<old-destination>,<new-destination>

  For example, to change the destination rs156 to rs999 in
  the USER range recovery plan for rs149, you could issue
  the following command:

  $ ht metalog_tool --balance-plan-change-destination rs149,USER,rs156,rs999 /hypertable/servers/master/log/mml

  Valid values for the <plan-type> field are: ROOT, METADATA,
  SYSTEM, and USER.  The <failed-server> and <plan-type>
  fields can hold the wildcard character ('*') to man all
  failed servers and all plan types, respectively:

  $ ht metalog_tool --balance-plan-change-destination *,*,rs156,rs999 /hypertable/servers/master/log/mml

  The BalancePlanAuthority entity also has what's known as
  the "current set".  This is the set of move plans for all
  non-recovery related moves.  To clear this set, use the
  --balance-plan-clear-current-set option:

  $ ht metalog_tool --balance-plan-clear-current-set /hypertable/servers/master/log/mml

  To change the destination of any OperationMoveRange
  entities, use the --change-move-destination option.  The
  argument to this option takes the following form:

  rsN-rsM,rsO-rsP,...

  For example, to change the destination of any
  OperationMoveRange entities from rs1 to rs3, you can use
  the following command:

  $ ht metalog_tool --change-move-destination rs1-rs3 /hypertable/servers/master/log/mml

RSML OPTIONS

  To dump the Range entities from an RSML in .tsv format,
  use the --dump option in combination with the
  --metadata-tsv option.  The --location option is required
  for specifying the value of the Location column:

  $ ht metalog_tool --dump --metadata-tsv --location rs1 /hypertable/servers/rs1/log/rsml
  2:000991891         StartRow              
  2:000991891         Location              rs1
  2:390849439         StartRow              375144033
  2:390849439         Location              rs1
  ...

  To set the "needs compaction" bit on all of the Range
  entities in an RSML, use the --needs-compaction option:

  $ ht metalog_tool --needs-compaction true /hypertable/servers/rs1/log/rsml

  To set the "needs compaction" bit on just a single entity,
  add the --select option (e.g. with entity ID 33):

  $ ht metalog_tool --needs-compaction true --select 33 /hypertable/servers/rs1/log/rsml

  To set the "acknowledge load" bit on all of the Range
  entities in an RSML, use the --acknowledge-load option:

  $ ht metalog_tool --acknowledge-load true /hypertable/servers/rs1/log/rsml

  To set the "acknowledge load" bit on just a single entity,
  add the --select option (e.g. with entity ID 33):

  $ ht metalog_tool --acknowledge-load true --select 33 /hypertable/servers/rs1/log/rsml

  To set the "soft limit" on all Range entities from a
  specific table, use the --soft-limit and --table options:

  $ ht metalog_tool --table 2 --soft-limit 777 /hypertable/servers/rs1/log/rsml

  To set the "soft limit" on a specific Range entity, add
  the --select option (e.g. with entity ID 33):

  $ ht metalog_tool --select 33 --table 2 --soft-limit 888 /hypertable/servers/rs1/log/rsml

  To change the start row of a Range entity, use the
  --translate-start-row-source and --translate-start-row-target
  options:

  $ ht metalog_tool --translate-start-row-source "old-start-row" --translate-start-row-target "new-start-row" /hypertable/servers/rs1/log/rsml

  To change the end row of a Range entity, use the
  --translate-end-row-source and --translate-end-row-target
  options:

  $ ht metalog_tool --translate-end-row-source "old-end-row" --translate-end-row-target "new-end-row" /hypertable/servers/rs1/log/rsml

Options)";

  struct AppPolicy : Config::Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
        ("all", "Used with --dump to display all entities in log (not just latest state)")
        ("balance-plan-change-destination", str(),
         "Change balance plan destination, format = <failed-server>,<plan-type>,<old-rs>,<new-rs>")
        ("balance-plan-drop-table", str(), "Drop table with this ID from balance plans")
        ("balance-plan-incr-generation", "Increment the generation number of the balance plan authority")
        ("balance-plan-clear-current-set", "Clears the \"current set\" of move specifications")
        ("change-move-destinations", str(), "Change move destinations, format is rsN-rsM,rsO-rsP,...")
        ("dump", "Display a textual representation of entities in log")
        ("metadata-tsv", "For each Range, dump StartRow and Location .tsv lines")
        ("select", str(),  "Apply changes on these entities")
        ("location", str()->default_value(""),
         "Used with --metadata-tsv to specify location proxy")
        ("purge", "Purge entities")
        ("acknowledge-load", boo(), "Set the load_acknowledged bit for each Range entity")
        ("needs-compaction", boo(), "Set the needs_compaction bit for each Range entity")
        ("table", str(),
         "Used with --soft-limit to specify applicable table")
        ("soft-limit", i64(),
         "Used to modify soft_limit in RSML entries")
        ("translate-start-row-source", str(), "Translate Range entities with this StartRow")
        ("translate-start-row-target", str(),
         "Translate Range entities matching source to this")
        ("translate-end-row-source", str(), "Translate Range entities with this EndRow")
        ("translate-end-row-target", str(),
         "Translate Range entities matching source to this")
        ("show-version", "Display log version number and exit")
        ;
      cmdline_hidden_desc().add_options()("log-path", str(), "dfs log path");
      cmdline_positional_desc().add("log-path", -1);
    }
    static void init() {
      if (!has("log-path")) {
        HT_ERROR_OUT <<"log-path required\n"<< cmdline_desc() << HT_END;
        exit(1);
      }
    }
  };

  class BalancePlanAuthorityChanges {
  public:
    BalancePlanAuthorityChanges() :
      increment_generation(false), clear_current_set(false) { }
    std::vector<String> destination_change_args;
    int destination_change_plan_type;
    String drop_table_id;
    bool increment_generation;
    bool clear_current_set;
  };

  typedef Meta::list<AppPolicy, FsClientPolicy, DefaultCommPolicy> Policies;

  void determine_log_type(FilesystemPtr &fs, String path, String &name, bool *is_file) {
    boost::trim_right_if(path, boost::is_any_of("/"));

    *is_file = false;

    const char *base, *ptr;
    if ((base = strrchr(path.c_str(), '/')) != (const char *)0) {
      for (ptr=base+1; *ptr && isdigit(*ptr); ptr++)
        ;
      if (*ptr == 0)
        *is_file = true;
    }

    if (*is_file) {
      int fd = fs->open(path, Filesystem::OPEN_FLAG_VERIFY_CHECKSUM);
      MetaLog::Header header;
      uint8_t buf[MetaLog::Header::LENGTH];
      const uint8_t *ptr = buf;
      size_t remaining = MetaLog::Header::LENGTH;

      size_t nread = fs->read(fd, buf, MetaLog::Header::LENGTH);

      if (nread != MetaLog::Header::LENGTH)
        HT_THROWF(Error::METALOG_BAD_HEADER,
                  "Short read of header for '%s' (expected %d, got %d)",
                  path.c_str(), (int)MetaLog::Header::LENGTH, (int)nread);

      header.decode(&ptr, &remaining);
      name = header.name;
      fs->close(fd);
    }
    else
      name = String(base+1);
  }

  std::map<String,String> translate_start;
  std::map<String,String> translate_end;

  std::set<String>  select_set;
  std::set<int64_t> select_ids;
  std::set<String> valid_ops;

  const char *valid_ops_array[] = {
    "BalancePlanAuthority",
    "OperationAlterTable",
    "OperationBalance",
    "OperationCreateNamespace",
    "OperationCreateTable",
    "OperationDropNamespace",
    "OperationDropTable",
    "OperationInitialize",
    "OperationMoveRange",
    "OperationRecover",
    "OperationRecoverRanges",
    "OperationRecoverServer",
    "OperationRenameTable",
    "OperationStop",
    "RangeServerConnection",
    (const char *)0
  };

  void parse_select_arg(const String &arg) {
    char_separator<char> sep(", ");
    tokenizer<char_separator<char> > tokens(arg, sep);
    BOOST_FOREACH(string t, tokens) {
      if (boost::starts_with(t, "rs")) {
	select_set.insert(t);
      }
      else if (valid_ops.count(t) > 0) {
	select_set.insert(t);
      }
      else {
	char *end;
	int64_t id = strtoll(t.c_str(), &end, 0);
	if (*end == '\0')
	  select_ids.insert(id);
	else {
	  cerr << "Unrecognized select entity: " << t << endl;
	  _exit(1);
	}
      }
    }
  }

  bool select_entity(MetaLog::EntityPtr &entity) {

    if (select_ids.empty() && select_set.empty())
      return true;

    if (!select_ids.empty() && select_ids.count(entity->id()) > 0)
      return true;

    if (!select_set.empty()) {
      RangeServerConnection *rsc = dynamic_cast<RangeServerConnection *>(entity.get());
      if (rsc != 0 && select_set.count(rsc->location()) > 0)
        return true;
      String name = entity->name();
      char_separator<char> sep(" \t");
      tokenizer<char_separator<char> > tokens(name, sep);
      if (select_set.count(*tokens.begin()) > 0)
        return true;
    }
    return false;
  }

  BalancePlanAuthorityChanges *parse_balance_plan_args() {
    BalancePlanAuthorityChanges *changes = 0;
    if (has("balance-plan-change-destination")) {
      changes = new BalancePlanAuthorityChanges();
      String arg = get_str("balance-plan-change-destination");
      char_separator<char> sep(",");
      tokenizer<char_separator<char> > tokens(arg, sep);
      size_t position = 0;
      BOOST_FOREACH(string t, tokens) {
        if (position == 0)
          changes->destination_change_args.push_back(t);
        else if (position == 1) {
          if (t == "ROOT")
            changes->destination_change_plan_type = RangeSpec::ROOT;
          else if (t == "METADATA")
            changes->destination_change_plan_type = RangeSpec::METADATA;
          else if (t == "SYSTEM")
            changes->destination_change_plan_type = RangeSpec::SYSTEM;
          else if (t == "USER")
            changes->destination_change_plan_type = RangeSpec::USER;
          else
            changes->destination_change_plan_type = RangeSpec::UNKNOWN;
        }
        else if (position == 2)
          changes->destination_change_args.push_back(t);
        else if (position == 3)
          changes->destination_change_args.push_back(t);
        else
          HT_FATALF("Invalid arg format: --balance-plan-change-destination=%s",
                    arg.c_str());
        position++;
      }
      if (position != 4)
        HT_FATALF("Invalid arg format: --balance-plan-change-destination=%s",
                  arg.c_str());
    }
    if (has("balance-plan-drop-table") ||
        has("balance-plan-incr-generation") ||
        has("balance-plan-clear-current-set")) {
      if (changes == 0)
        changes = new BalancePlanAuthorityChanges();
      if (has("balance-plan-drop-table"))
        changes->drop_table_id = get_str("balance-plan-drop-table");
      changes->increment_generation = has("balance-plan-incr-generation");
      changes->clear_current_set = has("balance-plan-clear-current-set");
    }
    return changes;
  }

  typedef pair<String,String> MoveChangeSpec;

  void parse_change_move_destinations_arg(vector<MoveChangeSpec> &move_destination_changes) {
    if (has("change-move-destinations")) {
      String arg = get_str("change-move-destinations");
      boost::trim_if(arg, boost::is_any_of("'\""));
      char_separator<char> sep(",");
      tokenizer<char_separator<char> > tokens(arg, sep);
      MoveChangeSpec change;
      BOOST_FOREACH(string t, tokens) {
        const char *base = t.c_str();
        const char *ptr = base;
        while (*ptr && isalnum(*ptr))
          ptr++;
        change.first = t.substr(0, ptr-base);
        while (*ptr && !isalnum(*ptr))
          ptr++;
        if (*ptr == 0)
          HT_FATALF("Bad change-move-destinations specifier: %s", base);
        change.second = String(ptr, strlen(ptr));
        move_destination_changes.push_back(change);
      }
    }
  }


} // local namespace


int main(int argc, char **argv) {
  try {
    init_with_policies<Policies>(argc, argv);

    ConnectionManagerPtr conn_manager_ptr = std::make_shared<ConnectionManager>();

    String log_path = get_str("log-path");
    String log_host = get("log-host", String());
    int timeout = has("dfs-timeout") ? get_i32("dfs-timeout") : 10000;
    bool dump_all = has("all");
    bool dump = has("dump");
    bool show_version = has("show-version");
    bool metadata_tsv = has("metadata-tsv");
    String location;
    bool select = has("select");
    bool purge = has("purge");
    bool acknowledge_load = has("acknowledge-load");
    bool set_needs_compaction = has("needs-compaction");
    bool needs_compaction = false;
    bool translate=false;
    bool change_soft_limit = has("soft-limit");
    int64_t soft_limit = 0;
    String table;
    BalancePlanAuthorityChanges *bpa_changes = parse_balance_plan_args();
    String start_row, end_row;
    vector<MoveChangeSpec> move_destination_changes;

    parse_change_move_destinations_arg(move_destination_changes);

    for (size_t i=0; valid_ops_array[i]; i++)
      valid_ops.insert(valid_ops_array[i]);

    if (set_needs_compaction)
      needs_compaction = get_bool("needs-compaction");

    if (change_soft_limit) {
      if (!has("table"))
	HT_FATAL("--table switch must be supplied with --soft-limit");
      table = get_str("table");
      soft_limit = get_i64("soft-limit");
    }

    if (select)
      parse_select_arg(get_str("select"));

    if (has("translate-start-row-source") || has("translate-start-row-target")) {
      HT_ASSERT(has("translate-start-row-source") && has("translate-start-row-target"));
      String src, target;
      src = get_str("translate-start-row-source");
      target = get_str("translate-start-row-target");
      translate_start[src] = target;
      translate=true;
    }
    if (has("translate-end-row-source") || has("translate-end-row-target")) {
      HT_ASSERT(has("translate-end-row-source") && has("translate-end-row-target"));
      String src, target;
      src = get_str("translate-end-row-source");
      target = get_str("translate-end-row-target");
      translate_end[src] = target;
      translate=true;
    }

    if (metadata_tsv) {
      if (has("location") && get_str("location").size()>0)
        location = get_str("location");
      else
        location = FileUtils::file_to_string(System::install_dir + (String)"/run/location");
    }

    /**
     * Check for and connect to commit log DFS broker
     */
    FsBroker::Lib::ClientPtr dfs_client;

    if (log_host.length()) {
      int log_port = get_i16("log-port");
      InetAddr addr(log_host, log_port);

      dfs_client = std::make_shared<FsBroker::Lib::Client>(conn_manager_ptr, addr, timeout);
    }
    else {
      dfs_client = std::make_shared<FsBroker::Lib::Client>(conn_manager_ptr, properties);
    }

    if (!dfs_client->wait_for_connection(timeout)) {
      HT_ERROR("Unable to connect to DFS Broker, exiting...");
      exit(1);
    }

    // Population Defintion map
    unordered_map<String, MetaLog::DefinitionPtr> defmap;
    MetaLog::DefinitionPtr def = std::make_shared<MetaLog::DefinitionRangeServer>("");
    defmap[def->name()] = def;
    def = std::make_shared<MetaLog::DefinitionMaster>("");
    defmap[def->name()] = def;

    FilesystemPtr fs = static_pointer_cast<Filesystem>(dfs_client);
    MetaLog::ReaderPtr rsml_reader;
    String name;
    bool is_file;

    determine_log_type(fs, log_path, name, &is_file);

    unordered_map<String, MetaLog::DefinitionPtr>::iterator iter = defmap.find(name);
    if (iter == defmap.end()) {
      cerr << "No definition for log type '" << name << "'" << endl;
      exit(1);
    }
    def = iter->second;

    int reader_flags = dump_all ? MetaLog::Reader::LOAD_ALL_ENTITIES : 0;
    if (is_file) {
      rsml_reader = std::make_shared<MetaLog::Reader>(fs, def, reader_flags);
      rsml_reader->load_file(log_path);
    }
    else
      rsml_reader = std::make_shared<MetaLog::Reader>(fs, def, log_path, reader_flags);

    if (!metadata_tsv)
      cout << "log version: " << rsml_reader->version() << "\n";

    if (show_version)
      _exit(0);

    std::vector<MetaLog::EntityPtr> entities;

    if (dump_all)
      rsml_reader->get_all_entities(entities);
    else
      rsml_reader->get_entities(entities);

    if (dump) {
      if (metadata_tsv) {
        MetaLogEntityRange *entity_range;
        TableIdentifier table;
        for (size_t i=0; i<entities.size(); i++) {
          entity_range = dynamic_cast<MetaLogEntityRange *>(entities[i].get());
          if (entity_range && select_entity(entities[i])) {
            entity_range->get_boundary_rows(start_row, end_row);
            cout << entity_range->get_table_id() << ":" << end_row << "\tStartRow\t" << start_row << "\n";
            cout << entity_range->get_table_id() << ":" << end_row << "\tLocation\t" << location << "\n";
          }
        }
      }
      else {
        for (size_t i=0; i<entities.size(); i++)
          cout << *entities[i] << "\n";
      }
    }
    else {
      std::vector<MetaLog::EntityPtr> write_entities;
      size_t entities_purged = 0;
      size_t entities_translated = 0;
      size_t entities_modified = 0;
      BalancePlanAuthority *bpa;
      OperationMoveRange *move_entity;

      for (size_t i=0; i<entities.size(); i++) {

        if (!select_entity(entities[i])) {
	  write_entities.push_back(entities[i]);
          continue;
        }

        if (purge) {
	    entities_purged++;
            continue;
        }

        if (bpa_changes &&
            (bpa = dynamic_cast<BalancePlanAuthority *>(entities[i].get()))) {
          bool modified = false;
          if (!bpa_changes->drop_table_id.empty()) {
            bpa->remove_table_from_receiver_plan(bpa_changes->drop_table_id);
            modified = true;
          }
          if (!bpa_changes->destination_change_args.empty()) {
            bpa->change_receiver_plan_location(bpa_changes->destination_change_args[0],
                                               bpa_changes->destination_change_plan_type,
                                               bpa_changes->destination_change_args[1],
                                               bpa_changes->destination_change_args[2]);
            modified = true;
          }
          if (bpa_changes->increment_generation) {
            int current_generation = bpa->get_generation();
            bpa->set_generation(current_generation+1);
            modified = true;
          }
          if (bpa_changes->clear_current_set) {
            bpa->clear_current_set();
            modified = true;
          }
          if (modified)
            entities_modified++;
        }

        if (!move_destination_changes.empty() &&
            (move_entity = dynamic_cast<OperationMoveRange *>(entities[i].get()))) {
          String location = move_entity->get_location();
          BOOST_FOREACH(MoveChangeSpec &change, move_destination_changes) {
            if (change.first.compare(location) == 0) {
              move_entity->set_destination(change.second);
              entities_modified++;
              break;
            }
          }
        }

        if (translate) {
          MetaLogEntityRange *entity_range =
            dynamic_cast<MetaLogEntityRange *>(entities[i].get());
          if (entity_range) {
            entity_range->get_boundary_rows(start_row, end_row);
            if (translate_start.find(start_row) != translate_start.end()) {
              entity_range->set_start_row(translate_start[start_row]);
              entities_translated++;
              entities_modified++;
            }
            if (translate_end.find(end_row) != translate_end.end()) {
              entity_range->set_end_row(translate_end[end_row]);
              entities_translated++;
              entities_modified++;
            }
          }
        }

        if (acknowledge_load || change_soft_limit || set_needs_compaction) {
	  MetaLogEntityRange *entity_range =
	    dynamic_cast<MetaLogEntityRange *>(entities[i].get());
	  if (entity_range) {
	    bool modified = false;
	    if (acknowledge_load) {
	      entity_range->set_load_acknowledged(true);
	      modified = true;
	    }
	    if (change_soft_limit && !strcmp(entity_range->get_table_id(), table.c_str())) {
	      entity_range->set_soft_limit(soft_limit);
	      modified = true;
	    }
	    if (set_needs_compaction) {
	      entity_range->set_needs_compaction(needs_compaction);
	      modified = true;
	    }
	    if (modified)
	      entities_modified++;
	  }
        }

        write_entities.push_back(entities[i]);
      }

      MetaLog::WriterPtr rsml_writer(new MetaLog::Writer(fs, def, log_path, write_entities));
      rsml_writer->close();
      cout << entities_purged << " entities purged." << endl;
      cout << entities_translated << " entities translated." << endl;
      cout << entities_modified << " entities modified." << endl;
    }

    cout << flush;

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
  return 0;
}
