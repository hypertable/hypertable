/* -*- c++ -*-
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

#ifndef Hypertable_Lib_HqlParser_h
#define Hypertable_Lib_HqlParser_h

//#define BOOST_SPIRIT_DEBUG  ///$$$ DEFINE THIS WHEN DEBUGGING $$$///

#ifdef BOOST_SPIRIT_DEBUG
#define BOOST_SPIRIT_DEBUG_OUT std::cerr
#define HQL_DEBUG(_expr_) std::cerr << __func__ <<": "<< _expr_ << std::endl
#define HQL_DEBUG_VAL(str, val) HQL_DEBUG(str <<" val="<< val)
#else
#define HQL_DEBUG(_expr_)
#define HQL_DEBUG_VAL(str, val)
#endif

#include <Hypertable/Lib/BalancePlan.h>
#include <Hypertable/Lib/Cells.h>
#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/LoadDataFlags.h>
#include <Hypertable/Lib/LoadDataSource.h>
#include <Hypertable/Lib/RangeServer/Protocol.h>
#include <Hypertable/Lib/ScanSpec.h>
#include <Hypertable/Lib/Schema.h>
#include <Hypertable/Lib/SystemVariable.h>
#include <Hypertable/Lib/Table.h>
#include <Hypertable/Lib/TableParts.h>

#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/Logger.h>
#include <Common/Time.h>
#include <Common/TimeInline.h>

#include "HyperAppHelper/Unique.h"

#include <boost/algorithm/string.hpp>
#include <boost/spirit/include/classic_confix.hpp>
#include <boost/spirit/include/classic_core.hpp>
#include <boost/spirit/include/classic_escape_char.hpp>
#include <boost/spirit/include/classic_symbols.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

namespace Hypertable {
  using namespace Lib;
  namespace Hql {
    using namespace boost;
    using namespace boost::spirit;
    using namespace boost::spirit::classic;

    enum {
      COMMAND_HELP=1,
      COMMAND_CREATE_TABLE,
      COMMAND_DESCRIBE_TABLE,
      COMMAND_SHOW_CREATE_TABLE,
      COMMAND_SELECT,
      COMMAND_LOAD_DATA,
      COMMAND_INSERT,
      COMMAND_DELETE,
      COMMAND_GET_LISTING,
      COMMAND_DROP_TABLE,
      COMMAND_ALTER_TABLE,
      COMMAND_CREATE_SCANNER,
      COMMAND_DESTROY_SCANNER,
      COMMAND_FETCH_SCANBLOCK,
      COMMAND_LOAD_RANGE,
      COMMAND_SHUTDOWN,
      COMMAND_SHUTDOWN_MASTER,
      COMMAND_UPDATE,
      COMMAND_REPLAY_BEGIN,
      COMMAND_REPLAY_LOAD_RANGE,
      COMMAND_REPLAY_LOG,
      COMMAND_REPLAY_COMMIT,
      COMMAND_DROP_RANGE,
      COMMAND_DUMP,
      COMMAND_CLOSE,
      COMMAND_DUMP_TABLE,
      COMMAND_EXISTS_TABLE,
      COMMAND_USE_NAMESPACE,
      COMMAND_CREATE_NAMESPACE,
      COMMAND_DROP_NAMESPACE,
      COMMAND_RENAME_TABLE,
      COMMAND_WAIT_FOR_MAINTENANCE,
      COMMAND_BALANCE,
      COMMAND_HEAPCHECK,
      COMMAND_COMPACT,
      COMMAND_METADATA_SYNC,
      COMMAND_STOP,
      COMMAND_DUMP_PSEUDO_TABLE,
      COMMAND_SET,
      COMMAND_REBUILD_INDICES,
      COMMAND_STATUS,
      COMMAND_MAX
    };

   enum {
      RELOP_EQ=1,
      RELOP_LT,
      RELOP_LE,
      RELOP_GT,
      RELOP_GE,
      RELOP_SW
    };

    enum {
      BOOLOP_AND=0,
      BOOLOP_OR
    };

    enum {
      ALTER_ADD=1,
      ALTER_DROP,
      ALTER_RENAME_CF
    };

    enum {
      NO_QUALIFIER=1,
      EXACT_QUALIFIER,
      REGEXP_QUALIFIER,
      PREFIX_QUALIFIER
    };

    class InsertRecord {
    public:
      InsertRecord() 
        : timestamp(AUTO_ASSIGN), row_key_is_call(false), value_is_call(false) 
      { }

      void clear() {
        timestamp = AUTO_ASSIGN;
        row_key.clear();
        column_key.clear();
        value.clear();
        row_key_is_call=false;
        value_is_call=false;
      }
      ::int64_t timestamp;
      std::string row_key;
      bool row_key_is_call;
      std::string column_key;
      std::string value;
      bool value_is_call;
    };

    class RowInterval {
    public:
      RowInterval() : start_inclusive(true), start_set(false),
          end(Key::END_ROW_MARKER), end_inclusive(true), end_set(false) { }

      void clear() {
        start.clear();
        end = Key::END_ROW_MARKER;
        start_inclusive = end_inclusive = true;
        start_set = end_set = false;
      }
      bool empty() { return !(start_set || end_set); }

      void set_start(const std::string &s, bool inclusive) {
        HQL_DEBUG(s <<" inclusive="<< inclusive);
        start = s;
        start_inclusive = inclusive;
        start_set = true;
      }
      void set_end(const std::string &s, bool inclusive) {
        HQL_DEBUG(s <<" inclusive="<< inclusive);
        end = s;
        end_inclusive = inclusive;
        end_set = true;
      }
      std::string start;
      bool start_inclusive;
      bool start_set;
      std::string end;
      bool end_inclusive;
      bool end_set;
    };

    class CellInterval {
    public:
      CellInterval() : start_inclusive(true), start_set(false),
          end_inclusive(true), end_set(false) { }

      void clear() {
        start_row = start_column = "";
        end_row = end_column = "";
        start_inclusive = end_inclusive = true;
        start_set = end_set = false;
      }

      bool empty() { return !(start_set || end_set); }

      void set_start(const std::string &row, const std::string &column, bool inclusive) {
        HQL_DEBUG(row <<','<< column <<" inclusive="<< inclusive);
        start_row = row;
        start_column = column;
        start_inclusive = inclusive;
        start_set = true;
      }
      void set_end(const std::string &row, const std::string &column, bool inclusive) {
        HQL_DEBUG(row <<','<< column <<" inclusive="<< inclusive);
        end_row = row;
        end_column = column;
        end_inclusive = inclusive;
        end_set = true;
      }
      std::string start_row;
      std::string start_column;
      bool start_inclusive;
      bool start_set;
      std::string end_row;
      std::string end_column;
      bool end_inclusive;
      bool end_set;
    };

    class ScanState {
    public:
      ScanState() { }

      void set_time_interval(::int64_t start, ::int64_t end) {
        HQL_DEBUG("("<< start <<", "<< end <<")");
        builder.set_time_interval(start, end);
        start_time_set = end_time_set = true;
      }
      void set_start_time(::int64_t start) {
        HQL_DEBUG(start);
        builder.set_start_time(start);
        start_time_set = true;
      }
      void set_end_time(::int64_t end) {
        HQL_DEBUG(end);
        builder.set_end_time(end);
        end_time_set = true;
      }
      ::int64_t start_time() { return builder.get().time_interval.first; }
      ::int64_t end_time() { return builder.get().time_interval.second; }

      ScanSpecBuilder builder;
      std::string outfile;
      bool display_timestamps {};
      bool display_revisions {};
      bool keys_only {};
      std::string current_rowkey;
      bool current_rowkey_set {};
      RowInterval current_ri;
      CellInterval current_ci;
      std::string current_cell_row;
      std::string current_cell_column;
      bool    start_time_set {};
      bool    end_time_set {};
      ::int64_t current_timestamp {};
      bool    current_timestamp_set {};
      int current_relop {};
      int last_boolean_op {BOOLOP_AND};
      int buckets {};
    };

    class ParserState {
    public:
      ParserState() {
        System::initialize_tm(&tmval);
      }
      ParserState(NamespacePtr &ns) : ParserState() {
        nsp = ns;
      }
      NamespacePtr nsp;
      int command {};
      std::string ns;
      std::string table_name;
      std::string pseudo_table_name;
      std::string clone_table_name;
      std::string new_table_name;
      std::string str;
      std::string output_file;
      std::string input_file;
      std::string source;
      std::string destination;
      std::string rs_name;
      int input_file_src {};
      std::string header_file;
      int header_file_src {};
      ::uint32_t group_commit_interval {};
      ColumnFamilyOptions table_cf_defaults;
      AccessGroupOptions table_ag_defaults;
      std::vector<String> columns;
      std::string timestamp_column;
      int load_flags {};
      uint32_t flags {};
      SchemaPtr alter_schema;
      SchemaPtr create_schema;
      ColumnFamilySpec *cf_spec {};
      AccessGroupSpec *ag_spec {};
      std::map<std::string, ColumnFamilySpec *> new_cf_map;
      std::vector<ColumnFamilySpec *> new_cf_vector;
      std::map<std::string, ColumnFamilySpec *> modified_cf_map;
      std::vector<ColumnFamilySpec *> modified_cf_vector;
      std::map<std::string, AccessGroupSpec *> new_ag_map;
      std::vector<AccessGroupSpec *> new_ag_vector;
      std::map<std::string, AccessGroupSpec *> modified_ag_map;
      std::vector<AccessGroupSpec *> modified_ag_vector;
      std::set<std::string> new_cf;
      std::set<std::string> column_option_definition_set;
      std::set<std::string> access_group_option_definition_set;
      struct tm tmval;
      ::uint32_t nanoseconds {};
      double decimal_seconds {};
      ScanState scan;
      InsertRecord current_insert_value;
      CellsBuilder inserts;
      BalancePlan balance_plan;
      std::vector<String> delete_columns;
      bool delete_all_columns {};
      std::string delete_row;
      ::int64_t delete_time {};
      ::int64_t delete_version_time {};
      SystemVariable::Spec current_variable_spec;
      std::vector<SystemVariable::Spec> variable_specs;
      bool modify {};
      bool if_exists {};
      bool tables_only {};
      bool with_ids {};
      bool replay {};
      std::string range_start_row;
      std::string range_end_row;
      ::int32_t scanner_id {-1};
      ::int32_t row_uniquify_chars {};
      bool escape {true};
      bool nokeys {};
      std::string current_rename_column_old_name;
      std::string current_column_family;
      std::string current_column_predicate_name;
      std::string current_column_predicate_qualifier;
      uint32_t current_column_predicate_operation {};
      char field_separator {};

      void validate_function(const std::string &s) {
        if (s=="guid")
          return;
        HT_THROW(Error::HQL_PARSE_ERROR, String("Unknown function "
                "identifier '") + s + "()'");
      }

      void execute_all_functions(InsertRecord &rec) {
        if (rec.row_key_is_call) {
          execute_function(rec.row_key);
          rec.row_key_is_call=false;
        }
                
        if (rec.value_is_call) {
          execute_function(rec.value);
          rec.value_is_call=false;
        }
      }

      void execute_function(std::string &s) {
        if (s=="guid") {
          s=HyperAppHelper::generate_guid();
          return;
        }

        HT_THROW(Error::HQL_PARSE_ERROR, String("Unknown function "
                "identifier '") + s + "()'");
      }

      void check_and_set_column_option(const std::string &name,
                                       const std::string &option) {
        std::string key = name + "." + option;
        if (column_option_definition_set.count(key) > 0)
          HT_THROWF(Error::HQL_PARSE_ERROR,
                    "Multiple specifications of %s option for column '%s'",
                    option.c_str(), name.c_str());
        column_option_definition_set.insert(key);
      }

      void check_and_set_access_group_option(const std::string &name,
                                       const std::string &option) {
        std::string key = name + "." + option;
        if (access_group_option_definition_set.count(key) > 0)
          HT_THROWF(Error::HQL_PARSE_ERROR,
                    "Multiple specifications of %s option for access group '%s'",
                    option.c_str(), name.c_str());
        access_group_option_definition_set.insert(key);
      }

      ColumnFamilySpec *get_new_column_family(const std::string &name) {
        auto iter = new_cf_map.find(name);
        if (iter != new_cf_map.end())
          return iter->second;
        ColumnFamilySpec *cf = new ColumnFamilySpec(name);
        new_cf_map[name] = cf;
        new_cf_vector.push_back(cf);
        return cf;
      }

      ColumnFamilySpec *get_modified_column_family(const std::string &name) {
        auto iter = modified_cf_map.find(name);
        if (iter != modified_cf_map.end())
          return iter->second;
        HT_ASSERT(alter_schema);
        ColumnFamilySpec *existing_cf {};
        // Search for existing CF in modified AGs
        for (auto ag : modified_ag_vector) {
          if ((existing_cf = ag->get_column(name)) != nullptr)
            break;
        }
        // If not found in modified AGs, fetch from schema
        if (existing_cf == nullptr) {
          existing_cf = alter_schema->get_column_family(name);
          if (existing_cf == nullptr)
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Attempt to modify column '%s' which does not exist",
                      name.c_str());
        }
        ColumnFamilySpec *new_cf = new ColumnFamilySpec(name);
        new_cf->set_id(existing_cf->get_id());
        new_cf->set_access_group(existing_cf->get_access_group());
        new_cf->set_value_index(existing_cf->get_value_index());
        new_cf->set_qualifier_index(existing_cf->get_qualifier_index());
        modified_cf_map[name] = new_cf;
        modified_cf_vector.push_back(new_cf);
        return new_cf;
      }

      AccessGroupSpec *create_new_access_group(const std::string &name) {
        if (new_ag_map.find(name) != new_ag_map.end())
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Multiple definitions for access group '%s'",
                    name.c_str());
        AccessGroupSpec *ag = new AccessGroupSpec(name);
        new_ag_map[name] = ag;
        new_ag_vector.push_back(ag);
        return ag;
      }

      AccessGroupSpec *find_new_access_group(const std::string &name) {
        auto iter = new_ag_map.find(name);
        if (iter != new_ag_map.end())
          return iter->second;
        return nullptr;
      }

      AccessGroupSpec *create_modified_access_group(const std::string &name) {
        auto iter = modified_ag_map.find(name);
        if (iter != modified_ag_map.end())
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Attempt to modify access group '%s' twice in the same command",
                    name.c_str());
        HT_ASSERT(alter_schema);
        auto existing_ag = alter_schema->get_access_group(name);
        if (existing_ag == nullptr)
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Attempt to modify access group '%s' which does not exist",
                    name.c_str());
        AccessGroupSpec *ag = new AccessGroupSpec(name);
        modified_ag_map[name] = ag;
        modified_ag_vector.push_back(ag);
        return ag;
      }

      AccessGroupSpec *find_modified_access_group(const std::string &name) {
        auto iter = modified_ag_map.find(name);
        if (iter != modified_ag_map.end())
          return iter->second;
        return nullptr;
      }

      ColumnFamilySpec *find_column_family_in_modified_ag(const std::string &name) {
        for (auto ag : modified_ag_vector) {
          auto cf = ag->get_column(name);
          if (cf)
            return cf;
        }
        return nullptr;
      }

      bool ag_spec_is_new() {
        return create_schema ||
          (alter_schema && new_ag_map.find(ag_spec->get_name()) != new_ag_map.end());
      }

    };

    /// Creates string with outer quotes stripped off.
    /// @param str Pointer to c-style string
    /// @param len Length of string
    /// @return std::string with outer quotes stripped off
    inline std::string strip_quotes(const char *str, size_t len) {
      if (len > 1 && (*str == '\'' || *str == '"') && *str == *(str+len-1))
        return String(str+1, len-2);
      return String(str, len);
    }

    inline std::string regex_from_literal(const char *str, size_t len) {
      if (len > 0 && *str != '/')
        return strip_quotes(str, len);
      std::string regex = String(str+1, len-2);
      std::string oldStr("\\/");
      std::string newStr("/");
      size_t pos = 0;
      while((pos = regex.find(oldStr, pos)) != std::string::npos) {
        regex.replace(pos, oldStr.length(), newStr);
        pos += newStr.length();
      }
      return regex;
    }

    inline bool invalid_column_name(const std::string &name) {
      if (!isalpha(name[0]))
        return false;
      std::function<bool(char)> valid_char_predicate =
        [](char c) -> bool {return isalnum(c) || c == '_' || c == '-';};
      return find_if_not(name.begin(), name.end(), valid_char_predicate) != name.end();
    }

    struct set_command {
      set_command(ParserState &state, int cmd) : state(state), command(cmd) { }
      void operator()(char const *, char const *) const {
        state.command = command;
      }
      ParserState &state;
      int command;
    };

    struct set_namespace{
      set_namespace(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.ns = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_rangeserver {
      set_rangeserver(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.rs_name = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_table_name {
      set_table_name(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = String(str, end-str);
        const char *pseudo = strstr(name.c_str(), "^.");
        if (pseudo) {
          state.table_name = String(name.c_str(), pseudo);
          trim_if(state.table_name, is_any_of("'\""));
          state.pseudo_table_name = String(pseudo+1);
        }
        else {
          state.table_name = name;
          trim_if(state.table_name, is_any_of("'\""));
        }
      }
      ParserState &state;
    };

    struct set_new_table_name {
      set_new_table_name(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.new_table_name = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct start_alter_table {
      start_alter_table(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        if (strstr(name.c_str(), "^."))
          HT_THROWF(Error::UNSUPPORTED_OPERATION,
                    "Psudo-table '%s' schema cannot be altered", name.c_str());
        state.table_name = name;
        HT_ASSERT(state.nsp);
        state.alter_schema = state.nsp->get_schema(name);
      }
      ParserState &state;
    };

    struct set_clone_table_name {
      set_clone_table_name(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.clone_table_name = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_range_start_row {
      set_range_start_row(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.range_start_row = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_range_end_row {
      set_range_end_row(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.range_end_row = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_source {
      set_source(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.source = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_destination {
      set_destination(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.destination = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_balance_algorithm {
      set_balance_algorithm(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.balance_plan.algorithm = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct add_range_move_spec {
      add_range_move_spec(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        RangeMoveSpecPtr move_spec = std::make_shared<RangeMoveSpec>();
        move_spec->table.set_id(state.table_name);
        move_spec->range.set_start_row(state.range_start_row);
        move_spec->range.set_end_row(state.range_end_row);
        move_spec->source_location = state.source;
        move_spec->dest_location = state.destination;
        state.balance_plan.moves.push_back(move_spec);
      }
      ParserState &state;
    };

    struct balance_set_duration {
      balance_set_duration(ParserState &state) : state(state) { }
      void operator()(size_t duration) const {
        state.balance_plan.duration_millis = 1000*duration;
      }
      ParserState &state;
    };

    struct set_if_exists {
      set_if_exists(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.if_exists = true;
      }
      ParserState &state;
    };

    struct set_tables_only {
      set_tables_only(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.tables_only = true;
      }
      ParserState &state;
    };

    struct set_with_ids {
      set_with_ids(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.with_ids = true;
      }
      ParserState &state;
    };

    struct set_replay {
      set_replay(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.replay = true;
      }
      ParserState &state;
    };

    struct start_create_table_statement {
      start_create_table_statement(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        state.table_name = name;
        state.create_schema = std::make_shared<Schema>();
      }
      ParserState &state;
    };

    struct finish_create_table_statement {
      finish_create_table_statement(ParserState &state) : state(state) { }
      void operator()(char const *, char const *) const {

        if (!state.clone_table_name.empty()) {
          std::string schema_str = state.nsp->get_schema_str(state.clone_table_name, true);
          state.create_schema.reset(Schema::new_instance(schema_str));
          state.create_schema->clear_generation();
        }
        else if (state.input_file.empty()) {

          state.create_schema->set_group_commit_interval(state.group_commit_interval);
          state.create_schema->set_access_group_defaults(state.table_ag_defaults);
          state.create_schema->set_column_family_defaults(state.table_cf_defaults);

          // Assign columns with no access group to "default" access group,
          // creating it if necessary
          for (auto cf : state.new_cf_vector) {
            // Verify that the column family was created
            if (state.new_cf.count(cf->get_name()) == 0)
              HT_THROWF(Error::HQL_BAD_COMMAND,
                        "Reference to undefined column '%s'", cf->get_name().c_str());
            // If column family access group empty, assign it to the "default"
            // access group
            if (cf->get_access_group().empty()) {
              cf->set_access_group("default");
              if (state.new_ag_map.find("default") == state.new_ag_map.end())
                state.create_new_access_group("default");
            }
            auto iter = state.new_ag_map.find(cf->get_access_group());
            HT_ASSERT(iter != state.new_ag_map.end());
            iter->second->add_column(cf);
          }

          // Add access groups to schema
          for (auto ag_spec : state.new_ag_vector)
            state.create_schema->add_access_group(ag_spec);

          state.create_schema->validate();
        }
        state.command = COMMAND_CREATE_TABLE;
      }
      ParserState &state;
    };

    struct finish_alter_table_statement {
      finish_alter_table_statement(ParserState &state) : state(state) { }
      void operator()(char const *, char const *) const {

        state.command = COMMAND_ALTER_TABLE;

        // If schema was supplied with WITH clause, just return
        if (!state.input_file.empty())
          return;

        // Verify column family modifications are OK then replace old column
        // family with modified one
        for (auto modified_cf : state.modified_cf_vector) {
          AccessGroupSpec *ag_spec {};
          auto iter = state.modified_ag_map.find(modified_cf->get_access_group());
          if (iter != state.modified_ag_map.end())
            ag_spec = iter->second;
          else
            ag_spec = state.alter_schema->get_access_group(modified_cf->get_access_group());
          HT_ASSERT(ag_spec);
          const ColumnFamilySpec *old_cf = ag_spec->replace_column(modified_cf);
          HT_ASSERT(old_cf);
          if (old_cf->get_option_time_order_desc() != modified_cf->get_option_time_order_desc())
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Modificaton of TIME ORDER option for column '%s' is not allowed.",
                      old_cf->get_name().c_str());
          if (old_cf->get_option_counter() != modified_cf->get_option_counter())
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Modificaton of COUNTER option for column '%s' is not allowed.",
                      old_cf->get_name().c_str());
          delete old_cf;
        }

        // Add new columns to the appropriate access group
        for (auto cf : state.new_cf_vector) {
          if (cf->get_access_group().empty()) {
            cf->set_access_group("default");
            if (state.new_ag_map.find("default") == state.new_ag_map.end() &&
                state.alter_schema->get_access_group("default") == nullptr)
              state.create_new_access_group("default");
          }
          AccessGroupSpec *ag = state.find_modified_access_group(cf->get_access_group());
          if (ag == nullptr) {
            ag = state.alter_schema->get_access_group(cf->get_access_group());
            if (ag == nullptr)
              ag = state.find_new_access_group(cf->get_access_group());
          }
          HT_ASSERT(ag);
          ag->add_column(cf);
        }

        // Add access groups to schema
        for (auto ag_spec : state.new_ag_vector)
          state.alter_schema->add_access_group(ag_spec);

        // Replace modified access groups.  Verify that all of the non-deleted
        // column families in each modified access group were moved from the old
        // access group to the modified one and move the deleted ones
        for (auto modified_ag_spec : state.modified_ag_vector) {
          string ag_name = modified_ag_spec->get_name();
          auto old_ag_spec = state.alter_schema->get_access_group(ag_name);
          for (auto old_cf : old_ag_spec->columns()) {
            string cf_name = old_cf->get_name();
            if (!old_cf->get_deleted())
              HT_THROWF(Error::HQL_BAD_COMMAND,
                        "Missing column '%s' from modified access group '%s'",
                        cf_name.c_str(), ag_name.c_str());
            modified_ag_spec->add_column(old_ag_spec->get_column(cf_name));
          }
          old_ag_spec->clear_columns();
          old_ag_spec = state.alter_schema->replace_access_group(modified_ag_spec);
          delete old_ag_spec;
        }

        state.alter_schema->validate();
      }
      ParserState &state;
    };

    struct set_modify_flag {
      set_modify_flag(ParserState &state, bool val) : state(state), value(val) { }
      void operator()(char const *str, char const *end) const {
        state.modify = value;
      }
      ParserState &state;
      bool value;
    };

    struct open_column_family {
      open_column_family(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);

        if (state.modify) {
          state.cf_spec = state.get_modified_column_family(name);
        }
        else {

          // sanity check name
          if (invalid_column_name(name))
            HT_THROWF(Error::HQL_PARSE_ERROR, "Invalid column name %s",
                      name.c_str());

          // Make sure column being added doesn't already exist
          if (state.alter_schema) {
            if (state.alter_schema->get_column_family(name))
              HT_THROWF(Error::HQL_BAD_COMMAND,
                        "Attempt to add column '%s' which already exists",
                        name.c_str());
          }

          if (state.new_cf.count(name) > 0)
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Column family '%s' multiply defined", name.c_str());

          state.new_cf.insert(name);
          state.cf_spec = state.get_new_column_family(name);
        }
      }
      ParserState &state;
    };

    struct open_existing_column_family {
      open_existing_column_family(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        HT_ASSERT(state.alter_schema);
        state.cf_spec = state.alter_schema->get_column_family(name);
        if (state.cf_spec == nullptr)
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Attempt to modify column '%s' which does not exist",
                    name.c_str());
      }
      ParserState &state;
    };

    struct close_column_family {
      close_column_family(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.cf_spec = nullptr;
      }
      ParserState &state;
    };

    struct drop_column_family {
      drop_column_family(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        HT_ASSERT(state.alter_schema);
        bool drop_from_modified_ag {};
        state.cf_spec = state.alter_schema->get_column_family(name);
        if (state.cf_spec == nullptr) {
          state.cf_spec = state.find_column_family_in_modified_ag(name);
          if (state.cf_spec == nullptr)
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Attempt to drop column '%s' which does not exist",
                      name.c_str());
          drop_from_modified_ag = true;
        }
        if (state.cf_spec->get_deleted())
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Attempt to drop column '%s' which is already dropped",
                    name.c_str());
        if (drop_from_modified_ag) {
          auto ag = state.find_modified_access_group(state.cf_spec->get_access_group());
          HT_ASSERT(ag);
          ag->drop_column(name);
        }
        else
          state.alter_schema->drop_column_family(name);
      }
      ParserState &state;
    };

    struct drop_value_index {
      drop_value_index(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        HT_ASSERT(state.alter_schema);
        ColumnFamilySpec *cf = state.find_column_family_in_modified_ag(name);
        if (cf == nullptr) {
          cf = state.alter_schema->get_column_family(name);
          if (cf == nullptr)
            HT_THROWF(Error::HQL_BAD_COMMAND, "Attempt to drop index from "
                      "column '%s' which does not exist", name.c_str());
        }
        cf->set_value_index(false);
      }
      ParserState &state;
    };

    struct drop_qualifier_index {
      drop_qualifier_index(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        HT_ASSERT(state.alter_schema);
        ColumnFamilySpec *cf = state.find_column_family_in_modified_ag(name);
        if (cf == nullptr) {
          cf = state.alter_schema->get_column_family(name);
          if (cf == nullptr)
            HT_THROWF(Error::HQL_BAD_COMMAND, "Attempt to drop index from "
                      "column '%s' which does not exist", name.c_str());
        }
        cf->set_qualifier_index(false);
      }
      ParserState &state;
    };

    struct set_rename_column_family_old_name {
      set_rename_column_family_old_name(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.current_rename_column_old_name = strip_quotes(str, end-str);
        HT_ASSERT(state.alter_schema);
        state.cf_spec = state.alter_schema->get_column_family(state.current_rename_column_old_name);
        if (state.cf_spec == nullptr)
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Attempt to rename column family '%s' which does not exist",
                    state.current_rename_column_old_name.c_str());
      }
      ParserState &state;
    };

    struct set_rename_column_family_new_name {
      set_rename_column_family_new_name(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string new_name = strip_quotes(str, end-str);
        if (state.alter_schema->get_column_family(new_name))
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Attempt to rename column '%s' to '%s', which already exists",
                    state.current_rename_column_old_name.c_str(),
                    new_name.c_str());
        state.alter_schema->rename_column_family(state.current_rename_column_old_name, new_name);
      }
      ParserState &state;
    };

    struct set_counter {
      set_counter(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        string value(str, end-str);
        bool bval {};
        if (!strcasecmp(value.c_str(), "counter") ||
            !strcasecmp(value.c_str(), "true"))
          bval = true;
        if (state.cf_spec) {
          state.check_and_set_column_option(state.cf_spec->get_name(), "COUNTER");
          state.cf_spec->set_option_counter(bval);
        }
        else if (state.ag_spec) {
          state.check_and_set_access_group_option(state.ag_spec->get_name(), "COUNTER");
          state.ag_spec->set_default_counter(bval);
        }
        else
          state.table_cf_defaults.set_counter(bval);
      }
      ParserState &state;
    };

    struct set_max_versions {
      set_max_versions(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        if (state.cf_spec) {
          state.check_and_set_column_option(state.cf_spec->get_name(), "MAX_VERSIONS");
          state.cf_spec->set_option_max_versions((::int32_t)strtol(str, 0, 10));
        }
        else if (state.ag_spec) {
          state.check_and_set_access_group_option(state.ag_spec->get_name(), "MAX_VERSIONS");
          state.ag_spec->set_default_max_versions((::int32_t)strtol(str, 0, 10));
        }
        else
          state.table_cf_defaults.set_max_versions((::int32_t)strtol(str, 0, 10));
      }
      ParserState &state;
    };

    struct set_time_order {
      set_time_order(ParserState &state) : state(state) { }
      void operator()(const char *str, const char *end) const {
        std::string val = String(str, end-str);
        to_lower(val);
        if (state.cf_spec) {
          state.check_and_set_column_option(state.cf_spec->get_name(), "TIME_ORDER");
          state.cf_spec->set_option_time_order_desc(val == "desc");
        }
        else if (state.ag_spec) {
          state.check_and_set_access_group_option(state.ag_spec->get_name(), "TIME_ORDER");
          state.ag_spec->set_default_time_order_desc(val == "desc");
        }
        else
          state.table_cf_defaults.set_time_order_desc(val == "desc");
      }
      ParserState &state;
    };

    struct set_ttl {
      set_ttl(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        char *unit_ptr;
        double ttl = strtod(str, &unit_ptr);

        while(*unit_ptr == ' ' &&  unit_ptr < end )
          ++unit_ptr;

        if (unit_ptr < end) {
          std::string unit_str = String(unit_ptr, end-unit_ptr);
          to_lower(unit_str);

          if (unit_str.find("month") == 0)
            ttl *= 2592000.0;
          else if (unit_str.find("week") == 0)
            ttl *= 604800.0;
          else if (unit_str.find("day") == 0)
            ttl *= 86400.0;
          else if (unit_str.find("hour") == 0)
            ttl *= 3600.0;
          else if (unit_str.find("minute") == 0)
            ttl *= 60.0;
        }

        if (state.cf_spec) {
          state.check_and_set_column_option(state.cf_spec->get_name(), "TTL");
          state.cf_spec->set_option_ttl(ttl);
        }
        else if (state.ag_spec) {
          state.check_and_set_access_group_option(state.ag_spec->get_name(), "TTL");
          state.ag_spec->set_default_ttl(ttl);
        }
        else
          state.table_cf_defaults.set_ttl(ttl);
      }
      ParserState &state;
    };

    struct open_access_group {
      open_access_group(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        if (state.modify)
          state.ag_spec = state.create_modified_access_group(name);
        else
          state.ag_spec = state.create_new_access_group(name);
      }
      ParserState &state;
    };

    struct close_access_group {
      close_access_group(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.ag_spec = nullptr;
      }
      ParserState &state;
    };

    struct set_in_memory {
      set_in_memory(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        string value(str, end-str);
        bool bval {};
        if (!strcasecmp(value.c_str(), "in_memory") ||
            !strcasecmp(value.c_str(), "true"))
          bval = true;
        if (state.ag_spec) {
          if (!state.ag_spec_is_new() &&
              !state.ag_spec->get_option_in_memory())
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Modification of IN_MEMORY attribute for access group '%s'"
                      " is not supported", state.ag_spec->get_name().c_str());
          state.ag_spec->set_option_in_memory(bval);
        }
        else
          state.table_ag_defaults.set_in_memory(bval);
      }
      ParserState &state;
    };

    struct set_compressor {
      set_compressor(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string compressor = strip_quotes(str, end-str);
        to_lower(compressor);
        if (state.ag_spec) {
          if (!state.ag_spec_is_new() &&
              compressor != state.ag_spec->get_option_compressor())
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Modification of COMPRESSOR attribute for access group '%s'"
                      " is not supported", state.ag_spec->get_name().c_str());
          state.ag_spec->set_option_compressor(compressor);
        }
        else
          state.table_ag_defaults.set_compressor(compressor);
      }
      ParserState &state;
    };

    struct set_blocksize {
      set_blocksize(ParserState &state) : state(state) { }
      void operator()(size_t blocksize) const {
        if (state.ag_spec)
          state.ag_spec->set_option_blocksize(blocksize);
        else
          state.table_ag_defaults.set_blocksize(blocksize);
      }
      ParserState &state;
    };

    struct set_replication {
      set_replication(ParserState &state) : state(state) { }
      void operator()(size_t replication) const {
        if (state.ag_spec) {
          if (!state.ag_spec_is_new() &&
              replication != (size_t)state.ag_spec->get_option_replication())
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Modification of REPLICATION attribute for access group '%s'"
                      " is not supported", state.ag_spec->get_name().c_str());
          if (replication >= 32768)
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Invalid replication factor (%u) for access group '%s'",
                      (unsigned)replication, state.ag_spec->get_name().c_str());
          state.ag_spec->set_option_replication(replication);
        }
        else
          state.table_ag_defaults.set_replication(replication);
      }
      ParserState &state;
    };

    struct set_bloom_filter {
      set_bloom_filter(ParserState &state) : state(state) { }
      void operator()(char const * str, char const *end) const {
        std::string bloom_filter = strip_quotes(str, end-str);
        to_lower(bloom_filter);
        if (state.ag_spec) {
          if (!state.ag_spec_is_new() &&
              bloom_filter != state.ag_spec->get_option_bloom_filter())
            HT_THROWF(Error::HQL_BAD_COMMAND,
                      "Modification of BLOOMFILTER attribute for access group '%s'"
                      " is not supported", state.ag_spec->get_name().c_str());
          state.ag_spec->set_option_bloom_filter(bloom_filter);
        }
        else
          state.table_ag_defaults.set_bloom_filter(bloom_filter);
      }
      ParserState &state;
    };

    struct access_group_add_column_family {
      access_group_add_column_family(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        ColumnFamilySpec *cf = nullptr;
        std::string name = strip_quotes(str, end-str);

        if (state.modify) {
          if (state.ag_spec->get_column(name) != nullptr)
            HT_THROWF(Error::HQL_BAD_COMMAND, "Bad modify command - attempt to"
                      " add column '%s' to access group '%s' twice",
                      name.c_str(), state.ag_spec->get_name().c_str());
          cf = state.alter_schema->remove_column_family(name);
          if (cf && cf->get_access_group() != state.ag_spec->get_name())
            HT_THROWF(Error::HQL_BAD_COMMAND, "Bad modify command - moving"
                      " column '%s' from access group '%s' to '%s' is not "
                      "supported", name.c_str(), cf->get_access_group().c_str(),
                      state.ag_spec->get_name().c_str());
        }

        if (cf == nullptr)
          cf = state.get_new_column_family(name);

        if (cf->get_access_group().empty())
          cf->set_access_group(state.ag_spec->get_name());
        else if (cf->get_access_group() != state.ag_spec->get_name())
          HT_THROWF(Error::HQL_BAD_COMMAND, "Attempt to add column '%s' to "
                    "access group '%s' when it already belongs to access group"
                    " '%s'", name.c_str(), state.ag_spec->get_name().c_str(),
                    cf->get_access_group().c_str());

        if (state.modify)
          state.ag_spec->add_column(cf);

      }
      ParserState &state;
    };

    struct create_index {
      create_index(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        ColumnFamilySpec *cf = 0;

        if (state.alter_schema)
          cf = state.alter_schema->get_column_family(name);

        if (cf == 0)
          cf = state.get_new_column_family(name);

        if (cf->get_value_index())
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Index for column family '%s' %s defined",
                    name.c_str(), state.alter_schema ? "already" : "multiply");

        if (cf->get_option_counter())
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Attempt to define value index for COUNTER column '%s'",
                    name.c_str());
          
        cf->set_value_index(true);
      }
      ParserState &state;
    };

    struct create_qualifier_index {
      create_qualifier_index(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = strip_quotes(str, end-str);
        ColumnFamilySpec *cf = 0;

        if (state.alter_schema)
          cf = state.alter_schema->get_column_family(name);

        if (cf == 0)
          cf = state.get_new_column_family(name);

        if (cf->get_qualifier_index())
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Qualifier index for column family '%s' %s defined",
                    name.c_str(), state.alter_schema ? "already" : "multiply");

        if (cf->get_option_counter())
          HT_THROWF(Error::HQL_BAD_COMMAND,
                    "Attempt to define qualifier index for COUNTER column '%s'",
                    name.c_str());

        cf->set_qualifier_index(true);
      }
      ParserState &state;
    };

    struct scan_set_no_cache {
      scan_set_no_cache(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.builder.set_do_not_cache(true);
      }
      ParserState &state;
    };

    struct scan_set_row_regexp {
      scan_set_row_regexp(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string regexp(str, end-str);
        trim_if(regexp, boost::is_any_of("'\""));
        state.scan.builder.set_row_regexp(regexp.c_str());
      }
      ParserState &state;
    };

    struct scan_set_value_regexp {
      scan_set_value_regexp(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string regexp(str, end-str);
        trim_if(regexp, boost::is_any_of("'\""));
        state.scan.builder.set_value_regexp(regexp.c_str());
       }
       ParserState &state;
    };

    struct scan_set_column_predicate_name {
      scan_set_column_predicate_name(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.current_column_predicate_name = String(str, end-str);
       }
       ParserState &state;
    };

    struct scan_set_column_predicate_qualifier {
      scan_set_column_predicate_qualifier(ParserState &state, uint32_t operation)
        : state(state), operation(operation) { }
      void operator()(char const *str, char const *end) const {
        state.current_column_predicate_operation |= operation;
        if (operation == ColumnPredicate::QUALIFIER_REGEX_MATCH)
          state.current_column_predicate_qualifier =
            regex_from_literal(str, end-str);
        else
          state.current_column_predicate_qualifier =
            strip_quotes(str, end-str);
        state.scan.builder.set_and_column_predicates(state.scan.last_boolean_op == BOOLOP_AND);
      }
      void operator()(char c) const {
        HT_ASSERT(c == '*');
        state.current_column_predicate_operation |= 
          ColumnPredicate::QUALIFIER_PREFIX_MATCH;
        state.current_column_predicate_qualifier.clear();
        state.scan.builder.set_and_column_predicates(state.scan.last_boolean_op == BOOLOP_AND);
      }
      ParserState &state;
      uint32_t operation;
    };

    struct scan_set_column_predicate_value {
      scan_set_column_predicate_value(ParserState &state, uint32_t operation=0)
        : state(state), operation(operation) { }
      void operator()(char const *str, char const *end) const {
        std::string s;
        if (operation == ColumnPredicate::REGEX_MATCH)
          s = regex_from_literal(str, end-str);
        else
          s = strip_quotes(str, end-str);
        state.current_column_predicate_operation |= operation;
        state.scan.builder.add_column_predicate(state.current_column_predicate_name.c_str(),
                                                state.current_column_predicate_qualifier.c_str(),
                                                state.current_column_predicate_operation,
                                                s.c_str());
        state.current_column_predicate_name.clear();
        state.current_column_predicate_qualifier.clear();
        state.current_column_predicate_operation = 0;
        state.scan.builder.set_and_column_predicates(state.scan.last_boolean_op == BOOLOP_AND);
      }
      void operator()(char c) const {
        HT_ASSERT(c == ')');
        state.current_column_predicate_operation |= operation;
        state.scan.builder.add_column_predicate(state.current_column_predicate_name.c_str(),
                                                state.current_column_predicate_qualifier.c_str(),
                                                state.current_column_predicate_operation, "");
        state.current_column_predicate_name.clear();
        state.current_column_predicate_qualifier.clear();
        state.current_column_predicate_operation = 0;
        state.scan.builder.set_and_column_predicates(state.scan.last_boolean_op == BOOLOP_AND);
      }
      ParserState &state;
      uint32_t operation;
    };

    struct scan_set_column_predicate_operation {
      scan_set_column_predicate_operation(ParserState &state, uint32_t operation) 
          : state(state), operation(operation) { }
      void operator()(char c) const {
        state.scan.builder.add_column_predicate(state.current_column_predicate_name.c_str(),
                                                state.current_column_predicate_qualifier.c_str(),
                                                operation, "");
       }
       ParserState &state;
       uint32_t operation;
    };

    struct set_group_commit_interval {
      set_group_commit_interval(ParserState &state) : state(state) { }
      void operator()(size_t interval) const {
        if (state.group_commit_interval != 0)
          HT_THROW(Error::HQL_PARSE_ERROR, "GROUP_COMMIT_INTERVAL multiply defined");
        state.group_commit_interval = (::uint32_t)interval;
      }
      ParserState &state;
    };

    struct set_help {
      set_help(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.command = COMMAND_HELP;
        state.str = String(str, end-str);
        size_t offset = state.str.find_first_of(' ');
        if (offset != String::npos) {
          state.str = state.str.substr(offset+1);
          trim(state.str);
          to_lower(state.str);
        }
        else
          state.str = "";
      }
      ParserState &state;
    };

    struct set_str {
      set_str(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.str = String(str, end-str);
        trim_if(state.str, is_any_of("'\""));
      }
      ParserState &state;
    };

    struct set_output_file {
      set_output_file(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.output_file = String(str, end-str);
        trim_if(state.output_file, is_any_of("'\""));
        FileUtils::expand_tilde(state.output_file);
      }
      ParserState &state;
    };

    struct set_input_file {
      set_input_file(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string file = String(str, end-str);
        trim_if(file, is_any_of("'\""));

        if (boost::algorithm::starts_with(file, "fs://")) {
          state.input_file_src = DFS_FILE;
          state.input_file = file.substr(5);
        }
        else if (boost::algorithm::starts_with(file, "dfs://")) {
          state.input_file_src = DFS_FILE;
          state.input_file = file.substr(6);
        }
        else if (file.compare("-") == 0) {
          state.input_file_src = STDIN;
        }
        else {
          state.input_file_src = LOCAL_FILE;
          if (boost::algorithm::starts_with(file, "file://"))
            state.input_file = file.substr(7);
          else
            state.input_file = file;
          FileUtils::expand_tilde(state.input_file);
        }
      }

      ParserState &state;
    };

    struct set_header_file {
      set_header_file(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string file = String(str, end-str);
        trim_if(file, is_any_of("'\""));

        state.header_file_src = LOCAL_FILE;
        if (boost::algorithm::starts_with(file, "fs://") ||
            boost::algorithm::starts_with(file, "dfs://")) {
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "Header file must be in local filesystem, not in the brokered FS");
        }
        else {
          if (boost::algorithm::starts_with(file, "file://"))
            state.header_file = file.substr(7);
          else
            state.header_file = file;
          FileUtils::expand_tilde(state.header_file);
        }
      }
      ParserState &state;
    };

    struct set_dup_key_cols {
      set_dup_key_cols(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
	if (*str != '0' && strncasecmp(str, "no", 2) &&
	    strncasecmp(str, "off", 3) &&
	    strncasecmp(str, "false", 4))
	  state.load_flags |= LoadDataFlags::DUP_KEY_COLS;
      }
      ParserState &state;
    };

    struct set_dup_key_cols_true {
      set_dup_key_cols_true(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
	state.load_flags |= LoadDataFlags::DUP_KEY_COLS;
      }
      ParserState &state;
    };

    struct add_column {
      add_column(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string column(str, end-str);
        trim_if(column, is_any_of("'\""));
        state.columns.push_back(column);
      }
      ParserState &state;
    };

    struct set_timestamp_column {
      set_timestamp_column(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.timestamp_column = String(str, end-str);
        trim_if(state.timestamp_column, is_any_of("'\""));
      }
      ParserState &state;
    };

    struct set_row_uniquify_chars {
      set_row_uniquify_chars(ParserState &state) : state(state) { }
      void operator()(int nchars) const {
        state.row_uniquify_chars = nchars;
      }
      ParserState &state;
    };

    struct set_ignore_unknown_cfs {
      set_ignore_unknown_cfs(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.load_flags |= LoadDataFlags::IGNORE_UNKNOWN_COLUMNS;
      }
      ParserState &state;
    };

    struct set_single_cell_format {
      set_single_cell_format(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.load_flags |= LoadDataFlags::SINGLE_CELL_FORMAT;
      }
      ParserState &state;
    };

    struct set_field_separator {
      set_field_separator(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string field_separator(str, end-str);
        trim_if(field_separator, is_any_of("'\""));
        if (field_separator.length() != 1)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "Field separator must be a single character");
        state.field_separator = field_separator[0];
      }
      ParserState &state;
    };

    struct scan_add_column_family {
      scan_add_column_family(ParserState &state, int qualifier_flag=0) : state(state),
          qualifier_flag(qualifier_flag) { }
      void operator()(char const *str, char const *end) const {
        std::string column_name(str, end-str);
        trim_if(column_name, is_any_of("'\""));
        if (qualifier_flag == NO_QUALIFIER)
          state.scan.builder.add_column(column_name.c_str());
        else
          state.current_column_family = column_name;
      }
      ParserState &state;
      int qualifier_flag;
    };

    struct scan_add_column_qualifier {
      scan_add_column_qualifier(ParserState &state, int _qualifier_flag=0)
          : state(state), qualifier_flag(_qualifier_flag) { }
      void operator()(char const *str, char const *end) const {
        std::string qualifier(str, end-str);
        std::string qualified_column = state.current_column_family 
            + (qualifier_flag == PREFIX_QUALIFIER
                ? ":^"
                : ":")
            + qualifier;
        state.scan.builder.add_column(qualified_column.c_str());
      }
      void operator()(const char c) const {
        HT_ASSERT(c == '*');
        std::string qualified_column = state.current_column_family + ":*";
        state.scan.builder.add_column(qualified_column.c_str());
      }
      ParserState &state;
      int qualifier_flag;
    };


    struct scan_set_display_timestamps {
      scan_set_display_timestamps(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.display_timestamps=true;
      }
      ParserState &state;
    };

    struct scan_set_display_revisions {
      scan_set_display_revisions(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.display_revisions=true;
      }
      ParserState &state;
    };

    struct scan_clear_display_timestamps {
      scan_clear_display_timestamps(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.display_timestamps=false;
      }
      ParserState &state;
    };

    struct scan_add_row_interval {
      scan_add_row_interval(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        RowInterval &ri = state.scan.current_ri;
        HT_ASSERT(!ri.empty());
        state.scan.builder.add_row_interval(ri.start.c_str(),
            ri.start_inclusive, ri.end.c_str(), ri.end_inclusive);
        ri.clear();
      }
      ParserState &state;
    };

    struct scan_set_cell_row {
      scan_set_cell_row(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.current_cell_row = strip_quotes(str,end-str);
      }
      ParserState &state;
    };

    struct scan_set_cell_column {
      scan_set_cell_column(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        CellInterval &ci = state.scan.current_ci;
        std::string &row = state.scan.current_cell_row;
        std::string &column = state.scan.current_cell_column;

        column = String(str, end-str);
        trim_if(column, is_any_of("'\""));

        if (state.scan.current_relop) {
          switch (state.scan.current_relop) {
          case RELOP_EQ:
            if (!ci.empty())
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad cell expression");
            ci.set_start(row, column, true);
            ci.set_end(row, column, true);
            break;
          case RELOP_LT:
            if (ci.end_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad cell expression");
            ci.set_end(row, column, false);
            break;
          case RELOP_LE:
            if (ci.end_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad cell expression");
            ci.set_end(row, column, true);
            break;
          case RELOP_GT:
            if (ci.start_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad cell expression");
            ci.set_start(row, column, false);
            break;
          case RELOP_GE:
            if (ci.start_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad cell expression");
            ci.set_start(row, column, true);
            break;
          case RELOP_SW:
            if (!ci.empty())
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad cell expression");
            ci.set_start(row, column, true);
            column.append(1, static_cast<char>(0xff));
            ci.set_end(row, column, false);
            break;
          }
          if (!ci.end_set)
            ci.end_row = Key::END_ROW_MARKER;

          state.scan.builder.add_cell_interval(ci.start_row.c_str(),
              ci.start_column.c_str(), ci.start_inclusive, ci.end_row.c_str(),
              ci.end_column.c_str(), ci.end_inclusive);
          ci.clear();
          row.clear();
          column.clear();
          state.scan.current_relop = 0;
        }
      }
      ParserState &state;
    };

    struct scan_set_row {
      scan_set_row(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.current_rowkey = strip_quotes(str, end-str);
        if (state.scan.current_relop != 0) {
          switch (state.scan.current_relop) {
          case RELOP_EQ:
            if (!state.scan.current_ri.empty())
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad row expressions.");
            state.scan.current_ri.set_start(state.scan.current_rowkey, true);
            state.scan.current_ri.set_end(state.scan.current_rowkey, true);
            break;
          case RELOP_LT:
            if (state.scan.current_ri.end_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad row expressions.");
            state.scan.current_ri.set_end(state.scan.current_rowkey, false);
            break;
          case RELOP_LE:
            if (state.scan.current_ri.end_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad row expressions.");
            state.scan.current_ri.set_end(state.scan.current_rowkey, true);
            break;
          case RELOP_GT:
            if (state.scan.current_ri.start_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad row expressions.");
            state.scan.current_ri.set_start(state.scan.current_rowkey, false);
            break;
          case RELOP_GE:
            if (state.scan.current_ri.start_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad row expressions.");
            state.scan.current_ri.set_start(state.scan.current_rowkey, true);
            break;
          case RELOP_SW:
            if (!state.scan.current_ri.empty())
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad row expressions.");
            state.scan.current_ri.set_start(state.scan.current_rowkey, true);
            str = state.scan.current_rowkey.c_str();
            end = str + state.scan.current_rowkey.length();
            const char *ptr;
            for (ptr = end - 1; ptr > str; --ptr) {
              if (::uint8_t(*ptr) < 0xffu) {
                std::string temp_str(str, ptr - str);
                temp_str.append(1, (*ptr) + 1);
                state.scan.current_ri.set_end(temp_str, false);
                break;
              }
            }
            if (ptr == str) {
              state.scan.current_rowkey.append(4, (char)0xff);
              state.scan.current_ri.set_end(state.scan.current_rowkey, false);
            }
          }
          state.scan.current_rowkey_set = false;
          state.scan.current_relop = 0;
        }
        else {
          state.scan.current_rowkey_set = true;
          state.scan.current_cell_row.clear();
        }
      }
      ParserState &state;
    };

    struct scan_set_buckets {
      scan_set_buckets(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        if (state.scan.buckets != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT BUCKETS predicate multiply defined.");
        state.scan.buckets = ival;
      }
      ParserState &state;
    };

    struct scan_set_max_versions {
      scan_set_max_versions(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        if (state.scan.builder.get().max_versions != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT MAX_VERSIONS predicate multiply defined.");
        state.scan.builder.set_max_versions(ival);
      }
      ParserState &state;
    };

    struct scan_set_row_limit {
      scan_set_row_limit(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        if (state.scan.builder.get().row_limit != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT LIMIT predicate multiply defined.");
        state.scan.builder.set_row_limit(ival);
      }
      ParserState &state;
    };

    struct scan_set_cell_limit {
      scan_set_cell_limit(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        if (state.scan.builder.get().cell_limit != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT CELL_LIMIT predicate multiply defined.");
        state.scan.builder.set_cell_limit(ival);
      }
      ParserState &state;
    };

    struct scan_set_cell_limit_per_family {
      scan_set_cell_limit_per_family(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        if (state.scan.builder.get().cell_limit_per_family != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT CELL_LIMIT_PER_FAMILY predicate multiply defined.");
        state.scan.builder.set_cell_limit_per_family(ival);
      }
      ParserState &state;
    };

    struct scan_set_row_offset {
      scan_set_row_offset(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        if (state.scan.builder.get().row_offset != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT OFFSET predicate multiply defined.");
        if (state.scan.builder.get().cell_offset != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT OFFSET predicate not allowed in combination with "
                   "CELL_OFFSET.");
        state.scan.builder.set_row_offset(ival);
      }
      ParserState &state;
    };

    struct scan_set_cell_offset {
      scan_set_cell_offset(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        if (state.scan.builder.get().cell_offset != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT CELL_OFFSET predicate multiply defined.");
        if (state.scan.builder.get().row_offset != 0)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT OFFSET predicate not allowed in combination with "
                   "OFFSET.");
        state.scan.builder.set_cell_offset(ival);
      }
      ParserState &state;
    };

    struct scan_set_outfile {
      scan_set_outfile(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        if (state.scan.outfile != "")
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "SELECT INTO FILE multiply defined.");
        state.scan.outfile = String(str, end-str);
        trim_if(state.scan.outfile, is_any_of("'\""));
      }
      ParserState &state;
    };

    struct scan_set_year {
      scan_set_year(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        HQL_DEBUG_VAL("scan_set_year", ival);
        state.tmval.tm_year = ival - 1900;
      }
      ParserState &state;
    };

    struct scan_set_month {
      scan_set_month(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        HQL_DEBUG_VAL("scan_set_month", ival);
        state.tmval.tm_mon = ival-1;
      }
      ParserState &state;
    };

    struct scan_set_day {
      scan_set_day(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        HQL_DEBUG_VAL("scan_set_day", ival);
        state.tmval.tm_mday = ival;
      }
      ParserState &state;
    };

    struct scan_set_seconds {
      scan_set_seconds(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        HQL_DEBUG_VAL("scan_set_seconds", ival);
        state.tmval.tm_sec = ival;
      }
      ParserState &state;
    };

    struct scan_set_minutes {
      scan_set_minutes(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        HQL_DEBUG_VAL("scan_set_minutes", ival);
        state.tmval.tm_min = ival;
      }
      ParserState &state;
    };

    struct scan_set_hours {
      scan_set_hours(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        HQL_DEBUG_VAL("scan_set_hours", ival);
        state.tmval.tm_hour = ival;
      }
      ParserState &state;
    };

    struct scan_set_decimal_seconds {
      scan_set_decimal_seconds(ParserState &state) : state(state) { }
      void operator()(double dval) const {
        HQL_DEBUG_VAL("scan_set_decimal_seconds", dval);
        state.decimal_seconds = dval;
      }
      ParserState &state;
    };

    struct scan_set_nanoseconds {
      scan_set_nanoseconds(ParserState &state) : state(state) { }
      void operator()(int ival) const {
        HQL_DEBUG_VAL("scan_set_nanoseconds", ival);
        state.nanoseconds = ival;
      }
      ParserState &state;
    };

    struct scan_set_relop {
      scan_set_relop(ParserState &state, int relop)
        : state(state), relop(relop) { }
      void operator()(char const *str, char const *end) const {
        process();
      }
      void operator()(const char c) const {
        process();
      }
      void process() const {
        if (state.scan.current_timestamp_set) {
          switch (relop) {
          case RELOP_EQ:
            if (state.scan.start_time_set || state.scan.end_time_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_time_interval(state.scan.current_timestamp,
                                         state.scan.current_timestamp);
            break;
          case RELOP_GT:
            if (state.scan.end_time_set ||
                (state.scan.start_time_set &&
                 state.scan.start_time() >= state.scan.current_timestamp))
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_end_time(state.scan.current_timestamp);
            break;
          case RELOP_GE:
            if (state.scan.end_time_set ||
                (state.scan.start_time_set &&
                 state.scan.start_time() > state.scan.current_timestamp))
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_end_time(state.scan.current_timestamp + 1);
            break;
          case RELOP_LT:
            if (state.scan.start_time_set ||
                (state.scan.end_time_set &&
                 state.scan.start_time() <= (state.scan.current_timestamp + 1)))
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_start_time(state.scan.current_timestamp + 1);
            break;
          case RELOP_LE:
            if (state.scan.start_time_set ||
                (state.scan.end_time_set &&
                 state.scan.end_time() <= state.scan.current_timestamp))
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_start_time(state.scan.current_timestamp);
            break;
          case RELOP_SW:
            HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp operator (=^)");
          }
          state.scan.current_timestamp_set = false;
          state.scan.current_relop = 0;
        }
        else if (state.scan.current_rowkey_set) {
          HT_ASSERT(state.scan.current_rowkey_set);

          switch (relop) {
          case RELOP_EQ:
            state.scan.current_ri.set_start(state.scan.current_rowkey, true);
            state.scan.current_ri.set_end(state.scan.current_rowkey, true);
            break;
          case RELOP_LT:
            state.scan.current_ri.set_start(state.scan.current_rowkey, false);
            break;
          case RELOP_LE:
            state.scan.current_ri.set_start(state.scan.current_rowkey, true);
            break;
          case RELOP_GT:
            state.scan.current_ri.set_end(state.scan.current_rowkey, false);
            break;
          case RELOP_GE:
            state.scan.current_ri.set_end(state.scan.current_rowkey, true);
            break;
          case RELOP_SW:
            HT_THROW(Error::HQL_PARSE_ERROR, "Bad use of operator (=^)");
          }
          state.scan.current_rowkey_set = false;
          state.scan.current_relop = 0;
        }
        else if (state.scan.current_cell_row.size()
                 && state.scan.current_ci.empty()) {
          std::string &row = state.scan.current_cell_row;
          std::string &column = state.scan.current_cell_column;

          switch (relop) {
          case RELOP_EQ:
            state.scan.current_ci.set_start(row, column, true);
            state.scan.current_ci.set_end(row, column, true);
            break;
          case RELOP_LT:
            state.scan.current_ci.set_start(row, column, false);
            break;
          case RELOP_LE:
            state.scan.current_ci.set_start(row, column, true);
            break;
          case RELOP_GT:
            state.scan.current_ci.set_end(row, column, false);
            break;
          case RELOP_GE:
            state.scan.current_ci.set_end(row, column, true);
          case RELOP_SW:
            HT_THROW(Error::HQL_PARSE_ERROR, "Bad use of operator (=^)");
          }
          row.clear();
          column.clear();
          state.scan.current_relop = 0;
        }
        else
          state.scan.current_relop = relop;
      }
      ParserState &state;
      int relop;
    };

    struct scan_set_boolop {
      scan_set_boolop(ParserState &state, int boolop)
        : state(state), boolop(boolop) { }
      void operator()(char const *str, char const *end) const {
        state.scan.last_boolean_op = boolop;
      }
      ParserState &state;
      int boolop;
    };

    struct scan_set_time {
      scan_set_time(ParserState &state) : state(state){ }
      void operator()(char const *str, char const *end) const {
        std::string time_str = strip_quotes(str, end-str);

        try {
          state.scan.current_timestamp = parse_ts(time_str.c_str());
        }
        catch (std::runtime_error &e) {
          HT_THROWF(Error::HQL_PARSE_ERROR,
                    "Unable to parse timestamp - '%s'",
                    time_str.c_str());
        }
        if (state.scan.current_relop != 0) {
          switch (state.scan.current_relop) {
          case RELOP_EQ:
            if (state.scan.start_time_set || state.scan.end_time_set)
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_time_interval(state.scan.current_timestamp,
                                         state.scan.current_timestamp + 1);
            break;
          case RELOP_LT:
            if (state.scan.end_time_set ||
                (state.scan.start_time_set &&
                 state.scan.start_time() >= state.scan.current_timestamp))
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_end_time(state.scan.current_timestamp);
            break;
          case RELOP_LE:
            if (state.scan.end_time_set ||
                (state.scan.start_time_set &&
                 state.scan.start_time() > state.scan.current_timestamp))
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_end_time(state.scan.current_timestamp + 1);
            break;
          case RELOP_GT:
            if (state.scan.start_time_set ||
                (state.scan.end_time_set &&
                 state.scan.end_time() <= (state.scan.current_timestamp + 1)))
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_start_time(state.scan.current_timestamp + 1);
            break;
          case RELOP_GE:
            if (state.scan.start_time_set ||
                (state.scan.end_time_set &&
                 state.scan.end_time() <= state.scan.current_timestamp))
              HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp expression");
            state.scan.set_start_time(state.scan.current_timestamp);
            break;
          case RELOP_SW:
            HT_THROW(Error::HQL_PARSE_ERROR, "Bad timestamp operator (=^)");
          }
          state.scan.current_relop = 0;
          state.scan.current_timestamp_set = false;
        }
        else
          state.scan.current_timestamp_set = true;

        System::initialize_tm(&state.tmval);
        state.decimal_seconds = 0;
        state.nanoseconds = 0;
      }
      ParserState &state;
    };

    struct scan_set_return_deletes {
      scan_set_return_deletes(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.builder.set_return_deletes(true);
      }
      ParserState &state;
    };

    struct scan_set_scan_and_filter_rows {
      scan_set_scan_and_filter_rows(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.builder.set_scan_and_filter_rows(true);
      }
      ParserState &state;
    };

    struct set_noescape {
      set_noescape(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.escape = false;
        state.load_flags |= LoadDataFlags::NO_ESCAPE;
      }
      ParserState &state;
    };

    struct set_no_log {
      set_no_log(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.load_flags |= LoadDataFlags::NO_LOG;
      }
      ParserState &state;
    };

    struct scan_set_keys_only {
      scan_set_keys_only(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scan.keys_only=true;
        state.scan.builder.set_keys_only(true);
      }
      ParserState &state;
    };

    struct scan_set_debug {
      scan_set_debug(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        if (state.scan.builder.get().debug != nullptr)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   "DEBUG predicate multiply defined.");
        std::string debug = strip_quotes(str, end-str);
        state.scan.builder.set_debug(debug.c_str());
      }
      ParserState &state;
    };

    struct set_insert_timestamp {
      set_insert_timestamp(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string time_str = strip_quotes(str, end-str);

        try {
          state.current_insert_value.timestamp = parse_ts(time_str.c_str());
        }
        catch (std::runtime_error &e) {
          HT_THROWF(Error::HQL_PARSE_ERROR,
                    "Unable to parse INSERT timestamp - '%s'",
                    time_str.c_str());
        }
      }
      ParserState &state;
    };

    struct set_insert_rowkey {
      set_insert_rowkey(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.current_insert_value.row_key  =
          strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_insert_rowkey_call {
      set_insert_rowkey_call(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        to_lower(state.current_insert_value.row_key);
        state.validate_function(state.current_insert_value.row_key);
        state.current_insert_value.row_key_is_call = true;
      }
      ParserState &state;
    };

    struct set_insert_columnkey {
      set_insert_columnkey(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.current_insert_value.column_key = String(str, end-str);
        trim_if(state.current_insert_value.column_key, is_any_of("'\""));
      }
      ParserState &state;
    };

    struct set_insert_value {
      set_insert_value(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.current_insert_value.value = String(str, end-str);
        trim_if(state.current_insert_value.value, is_any_of("'\""));
      }
      ParserState &state;
    };

    struct set_insert_value_call {
      set_insert_value_call(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        to_lower(state.current_insert_value.value);
        state.validate_function(state.current_insert_value.value);
        state.current_insert_value.value_is_call = true;
      }
      ParserState &state;
    };

    struct add_insert_value {
      add_insert_value(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        const InsertRecord &rec = state.current_insert_value;
        char *cq;
        Cell cell;

        state.execute_all_functions(state.current_insert_value);

        cell.row_key = rec.row_key.c_str();
        cell.column_family = rec.column_key.c_str();

        if ((cq = (char*)strchr(rec.column_key.c_str(), ':')) != 0) {
          *cq++ = 0;
          cell.column_qualifier = cq;
        }
        cell.timestamp = rec.timestamp;
        cell.value = (::uint8_t *)rec.value.c_str();
        cell.value_len = rec.value.length();
        cell.flag = FLAG_INSERT;
        state.inserts.add(cell);
        state.current_insert_value.clear();
      }
      ParserState &state;
    };

    struct delete_column {
      delete_column(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string column(str, end-str);
        trim_if(column, is_any_of("'\""));
        state.delete_columns.push_back(column);
      }
      void operator()(const char c) const {
        state.delete_all_columns = true;
      }
      ParserState &state;
    };

    struct delete_set_row {
      delete_set_row(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.delete_row = strip_quotes(str, end-str);
      }
      ParserState &state;
    };

    struct set_delete_timestamp {
      set_delete_timestamp(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string time_str = strip_quotes(str, end-str);

        try {
          state.delete_time = parse_ts(time_str.c_str());
        }
        catch (std::runtime_error &e) {
          HT_THROWF(Error::HQL_PARSE_ERROR,
                    "Unable to parse DELETE timestamp - '%s'",
                    time_str.c_str());
        }
      }
      ParserState &state;
    };

    struct set_delete_version_timestamp {
      set_delete_version_timestamp(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string time_str = strip_quotes(str, end-str);

        try {
          state.delete_version_time = parse_ts(time_str.c_str());
        }
        catch (std::runtime_error &e) {
          HT_THROWF(Error::HQL_PARSE_ERROR,
                    "Unable to parse DELETE timestamp - '%s'",
                    time_str.c_str());
        }
      }
      ParserState &state;
    };

    struct set_scanner_id {
      set_scanner_id(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.scanner_id = (::uint32_t)strtol(str, 0, 10);
      }
      ParserState &state;
    };

    struct set_nokeys {
      set_nokeys(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        state.nokeys=true;
      }
      ParserState &state;
    };

    struct set_flags_range_type {
      set_flags_range_type(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string range_type_str = String(str, end-str);
        trim_if(range_type_str, is_any_of("'\""));
        to_lower(range_type_str);
        if (range_type_str == "all")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_ALL;
        else if (range_type_str == "root")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_ROOT;
        else if (range_type_str == "metadata")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_METADATA;
        else if (range_type_str == "system")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_SYSTEM;
        else if (range_type_str == "user")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_USER;
        else
          HT_THROW(Error::HQL_PARSE_ERROR,
                   format("Invalid range type specifier:  %s", range_type_str.c_str()));

      }
      ParserState &state;
    };

    struct set_flags_compaction_type {
      set_flags_compaction_type(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string compaction_type_str = String(str, end-str);
        trim_if(compaction_type_str, is_any_of("'\""));
        to_lower(compaction_type_str);
        if (compaction_type_str == "minor")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_MINOR;
        else if (compaction_type_str == "major")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_MAJOR;
        else if (compaction_type_str == "merging")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_MERGING;
        else if (compaction_type_str == "gc")
          state.flags |= RangeServer::Protocol::COMPACT_FLAG_GC;
        else
          HT_THROW(Error::HQL_PARSE_ERROR,
                   format("Invalid compaction type specifier:  %s", compaction_type_str.c_str()));

      }
      ParserState &state;
    };

    struct set_flags_index_type {
      set_flags_index_type(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string index_type_str = String(str, end-str);
        to_lower(index_type_str);
        if (index_type_str == "value")
          state.flags = TableParts::VALUE_INDEX;
        else if (index_type_str == "qualifier")
          state.flags = TableParts::QUALIFIER_INDEX;
        else
          HT_THROW(Error::HQL_PARSE_ERROR,
                   format("Invalid index type specifier:  %s", index_type_str.c_str()));
      }
      ParserState &state;
    };


    struct set_variable_name {
      set_variable_name(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string name = String(str, end-str);
        to_upper(name);
        state.current_variable_spec.code = SystemVariable::string_to_code(name);
        if (state.current_variable_spec.code == -1)
          HT_THROW(Error::HQL_PARSE_ERROR,
                   format("Unrecognized variable name: %s", name.c_str()));
      }
      ParserState &state;
    };

    struct set_variable_value {
      set_variable_value(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
        std::string value = String(str, end-str);
        to_lower(value);
        if (value == "true")
          state.current_variable_spec.value = true;
        else
          state.current_variable_spec.value = false;
        state.variable_specs.push_back(state.current_variable_spec);
      }
      ParserState &state;
    };


    struct Parser : grammar<Parser> {
      Parser(ParserState &state) : state(state) { }

      template <typename ScannerT>
      struct definition {
        definition(Parser const &self)  {
          keywords =
            "access", "ACCESS", "Access", "GROUP", "group", "Group",
            "from", "FROM", "From", "start_time", "START_TIME", "Start_Time",
            "Start_time", "end_time", "END_TIME", "End_Time", "End_time",
            "into", "INTO", "Into", "table", "TABLE", "Table", "NAMESPACE", "Namespace",
            "cells", "CELLS", "value", "VALUE", "regexp", "REGEXP", "wait", "WAIT"
            "for", "FOR", "maintenance", "MAINTENANCE", "index", "INDEX", 
            "qualifier", "QUALIFIER", "debug", "DEBUG";

          /**
           * OPERATORS
           */
          chlit<>     ONE('1');
          chlit<>     ZERO('0');
          chlit<>     SINGLEQUOTE('\'');
          chlit<>     DOUBLEQUOTE('"');
          chlit<>     QUESTIONMARK('?');
          chlit<>     PLUS('+');
          chlit<>     MINUS('-');
          chlit<>     STAR('*');
          chlit<>     SLASH('/');
          chlit<>     COMMA(',');
          chlit<>     SEMI(';');
          chlit<>     COLON(':');
          chlit<>     EQUAL('=');
          strlit<>    DOUBLEEQUAL("==");
          chlit<>     LT('<');
          strlit<>    LE("<=");
          strlit<>    GE(">=");
          chlit<>     GT('>');
          strlit<>    SW("=^");
          strlit<>    RE("=~");
          strlit<>    QUALPREFIX(":^");
          chlit<>     LPAREN('(');
          chlit<>     RPAREN(')');
          chlit<>     LBRACK('[');
          chlit<>     RBRACK(']');
          chlit<>     POINTER('^');
          chlit<>     DOT('.');
          strlit<>    DOTDOT("..");
          strlit<>    DOUBLEQUESTIONMARK("??");
          chlit<>     PIPE('|');

          /**
           * TOKENS
           */
          typedef inhibit_case<strlit<> > Token;

          Token CREATE       = as_lower_d["create"];
          Token DROP         = as_lower_d["drop"];
          Token BALANCE      = as_lower_d["balance"];
          Token DURATION     = as_lower_d["duration"];
          Token ADD          = as_lower_d["add"];
          Token MODIFY       = as_lower_d["modify"];
          Token USE          = as_lower_d["use"];
          Token RENAME       = as_lower_d["rename"];
          Token COLUMN       = as_lower_d["column"];
          Token COLUMNS      = as_lower_d["columns"];
          Token FAMILY       = as_lower_d["family"];
          Token ALTER        = as_lower_d["alter"];
          Token HELP         = as_lower_d["help"];
          Token NAMESPACE    = as_lower_d["namespace"];
          Token DATABASE     = as_lower_d["database"];
          Token TABLE        = as_lower_d["table"];
          Token TABLES       = as_lower_d["tables"];
          Token TO           = as_lower_d["to"];
          Token TTL          = as_lower_d["ttl"];
          Token TYPE         = as_lower_d["type"];
          Token COUNTER      = as_lower_d["counter"];
          Token MONTHS       = as_lower_d["months"];
          Token MONTH        = as_lower_d["month"];
          Token WEEKS        = as_lower_d["weeks"];
          Token WEEK         = as_lower_d["week"];
          Token DAYS         = as_lower_d["days"];
          Token DAY          = as_lower_d["day"];
          Token HOURS        = as_lower_d["hours"];
          Token HOUR         = as_lower_d["hour"];
          Token MINUTES      = as_lower_d["minutes"];
          Token MINUTE       = as_lower_d["minute"];
          Token SECONDS      = as_lower_d["seconds"];
          Token SECOND       = as_lower_d["second"];
          Token IN_MEMORY    = as_lower_d["in_memory"];
          Token BLOCKSIZE    = as_lower_d["blocksize"];
          Token ACCESS       = as_lower_d["access"];
          Token GROUP        = as_lower_d["group"];
          Token INDEX        = as_lower_d["index"];
          Token QUALIFIER    = as_lower_d["qualifier"];
          Token DESCRIBE     = as_lower_d["describe"];
          Token SHOW         = as_lower_d["show"];
          Token GET          = as_lower_d["get"];
          Token LISTING      = as_lower_d["listing"];
          Token ESC_HELP     = as_lower_d["\\h"];
          Token SELECT       = as_lower_d["select"];
          Token STOP         = as_lower_d["stop"];
          Token START_TIME   = as_lower_d["start_time"];
          Token END_TIME     = as_lower_d["end_time"];
          Token FROM         = as_lower_d["from"];
          Token WHERE        = as_lower_d["where"];
          Token REGEXP       = as_lower_d["regexp"];
          Token ROW          = as_lower_d["row"];
          Token CELL         = as_lower_d["cell"];
          Token CELLS        = as_lower_d["cells"];
          Token ROW_KEY_COLUMN          = as_lower_d["row_key_column"];
          Token TIMESTAMP_COLUMN        = as_lower_d["timestamp_column"];
          Token HEADER_FILE             = as_lower_d["header_file"];
          Token ROW_UNIQUIFY_CHARS      = as_lower_d["row_uniquify_chars"];
          Token IGNORE_UNKNOWN_CFS      = as_lower_d["ignore_unknown_cfs"];
          Token IGNORE_UNKNOWN_COLUMNS  = as_lower_d["ignore_unknown_columns"];
          Token DUP_KEY_COLS            = as_lower_d["dup_key_cols"];
          Token DUPLICATE_KEY_COLUMNS   = as_lower_d["duplicate_key_columns"];
          Token NO_TIMESTAMPS = as_lower_d["no_timestamps"];
          Token START_ROW    = as_lower_d["start_row"];
          Token END_ROW      = as_lower_d["end_row"];
          Token INCLUSIVE    = as_lower_d["inclusive"];
          Token EXCLUSIVE    = as_lower_d["exclusive"];
          Token MAX_VERSIONS = as_lower_d["max_versions"];
          Token REVS         = as_lower_d["revs"];
          Token LIMIT        = as_lower_d["limit"];
          Token CELL_LIMIT   = as_lower_d["cell_limit"];
          Token CELL_LIMIT_PER_FAMILY   = as_lower_d["cell_limit_per_family"];
          Token OFFSET       = as_lower_d["offset"];
          Token CELL_OFFSET  = as_lower_d["cell_offset"];
          Token INTO         = as_lower_d["into"];
          Token FILE         = as_lower_d["file"];
          Token LOAD         = as_lower_d["load"];
          Token DATA         = as_lower_d["data"];
          Token INFILE       = as_lower_d["infile"];
          Token TIMESTAMP    = as_lower_d["timestamp"];
          Token TIME_ORDER   = as_lower_d["time_order"];
          Token ASC          = as_lower_d["asc"];
          Token DESC         = as_lower_d["desc"];
          Token VERSION      = as_lower_d["version"];
          Token INSERT       = as_lower_d["insert"];
          Token DELETE       = as_lower_d["delete"];
          Token VALUE        = as_lower_d["value"];
          Token VALUES       = as_lower_d["values"];
          Token COMPRESSOR   = as_lower_d["compressor"];
          Token GROUP_COMMIT_INTERVAL   = as_lower_d["group_commit_interval"];
          Token DUMP         = as_lower_d["dump"];
          Token PSEUDO       = as_lower_d["pseudo"];
          Token STATS        = as_lower_d["stats"];
          Token STARTS       = as_lower_d["starts"];
          Token WITH         = as_lower_d["with"];
          Token IF           = as_lower_d["if"];
          Token NOT          = as_lower_d["not"];
          Token EXISTS       = as_lower_d["exists"];
          Token DISPLAY_TIMESTAMPS = as_lower_d["display_timestamps"];
          Token DISPLAY_REVISIONS = as_lower_d["display_revisions"];
          Token RETURN_DELETES = as_lower_d["return_deletes"];
          Token SCAN_AND_FILTER_ROWS = as_lower_d["scan_and_filter_rows"];
          Token KEYS_ONLY    = as_lower_d["keys_only"];
          Token RANGE        = as_lower_d["range"];
          Token UPDATE       = as_lower_d["update"];
          Token SCANNER      = as_lower_d["scanner"];
          Token ON           = as_lower_d["on"];
          Token DESTROY      = as_lower_d["destroy"];
          Token FETCH        = as_lower_d["fetch"];
          Token SCANBLOCK    = as_lower_d["scanblock"];
          Token CLOSE        = as_lower_d["close"];
          Token SHUTDOWN     = as_lower_d["shutdown"];
          Token MASTER       = as_lower_d["master"];
          Token REPLAY       = as_lower_d["replay"];
          Token START        = as_lower_d["start"];
          Token COMMIT       = as_lower_d["commit"];
          Token LOG          = as_lower_d["log"];
          Token BLOOMFILTER  = as_lower_d["bloomfilter"];
          Token TRUE         = as_lower_d["true"];
          Token FALSE        = as_lower_d["false"];
          Token AND          = as_lower_d["and"];
          Token OR           = as_lower_d["or"];
          Token LIKE         = as_lower_d["like"];
          Token NO_CACHE     = as_lower_d["no_cache"];
          Token NOESCAPE     = as_lower_d["noescape"];
          Token NO_ESCAPE    = as_lower_d["no_escape"];
          Token NO_LOG       = as_lower_d["no_log"];
          Token IDS          = as_lower_d["ids"];
          Token NOKEYS       = as_lower_d["nokeys"];
          Token SINGLE_CELL_FORMAT = as_lower_d["single_cell_format"];
          Token BUCKETS      = as_lower_d["buckets"];
          Token REPLICATION  = as_lower_d["replication"];
          Token WAIT         = as_lower_d["wait"];
          Token FOR          = as_lower_d["for"];
          Token MAINTENANCE  = as_lower_d["maintenance"];
          Token HEAPCHECK    = as_lower_d["heapcheck"];
          Token ALGORITHM    = as_lower_d["algorithm"];
          Token COMPACT      = as_lower_d["compact"];
          Token ALL          = as_lower_d["all"];
          Token ROOT         = as_lower_d["root"];
          Token METADATA     = as_lower_d["metadata"];
          Token SYSTEM       = as_lower_d["system"];
          Token USER         = as_lower_d["user"];
          Token RANGES       = as_lower_d["ranges"];
          Token MINOR        = as_lower_d["minor"];
          Token MAJOR        = as_lower_d["major"];
          Token MERGING      = as_lower_d["merging"];
          Token GC           = as_lower_d["gc"];
          Token FS           = as_lower_d["fs"];
          Token SET          = as_lower_d["set"];
          Token REBUILD      = as_lower_d["rebuild"];
          Token INDICES      = as_lower_d["indices"];
          Token STATUS       = as_lower_d["status"];
          Token DEBUG        = as_lower_d["debug"];

          /**
           * Start grammar definition
           */
          boolean_literal
            = lexeme_d[TRUE | FALSE]
            ;

          identifier
            = lexeme_d[(alpha_p >> *(alnum_p | '_')) - (keywords)];

          string_literal
            = single_string_literal
            | double_string_literal
            ;

          pseudo_table_reference
            = lexeme_d["^." >> +alnum_p >> *('.' >> +alnum_p)]
            ;

          parameter_list
            = ch_p('(') >> ch_p(')')
            ;

          single_string_literal
            = confix_p(SINGLEQUOTE, *lex_escape_ch_p, SINGLEQUOTE);

          double_string_literal
            = confix_p(DOUBLEQUOTE, *lex_escape_ch_p, DOUBLEQUOTE);

          user_identifier
            = identifier
            | string_literal
            ;

          string_literal
            = single_string_literal
            | double_string_literal
            ;

          table_identifier
            = user_identifier >> *pseudo_table_reference
            ;

          regexp_literal
            = confix_p(SLASH, *lex_escape_ch_p , SLASH);

          statement
            = select_statement[set_command(self.state, COMMAND_SELECT)]
            | use_namespace_statement[set_command(self.state,
                COMMAND_USE_NAMESPACE)]
            | create_namespace_statement[set_command(self.state,
                COMMAND_CREATE_NAMESPACE)]
            | create_table_statement[finish_create_table_statement(self.state)]
            | describe_table_statement[set_command(self.state,
                COMMAND_DESCRIBE_TABLE)]
            | load_data_statement[set_command(self.state, COMMAND_LOAD_DATA)]
            | show_statement[set_command(self.state, COMMAND_SHOW_CREATE_TABLE)]
            | help_statement[set_help(self.state)]
            | insert_statement[set_command(self.state, COMMAND_INSERT)]
            | delete_statement[set_command(self.state, COMMAND_DELETE)]
            | get_listing_statement[set_command(self.state,
                COMMAND_GET_LISTING)]
            | drop_namespace_statement[set_command(self.state, COMMAND_DROP_NAMESPACE)]
            | drop_table_statement[set_command(self.state, COMMAND_DROP_TABLE)]
            | rename_table_statement[set_command(self.state, COMMAND_RENAME_TABLE)]
            | alter_table_statement[finish_alter_table_statement(self.state)]
            | load_range_statement[set_command(self.state, COMMAND_LOAD_RANGE)]
            | dump_statement[set_command(self.state, COMMAND_DUMP)]
            | dump_table_statement[set_command(self.state, COMMAND_DUMP_TABLE)]
            | dump_pseudo_table_statement[set_command(self.state, COMMAND_DUMP_PSEUDO_TABLE)]
            | update_statement[set_command(self.state, COMMAND_UPDATE)]
            | create_scanner_statement[set_command(self.state,
                COMMAND_CREATE_SCANNER)]
            | destroy_scanner_statement[set_command(self.state,
                COMMAND_DESTROY_SCANNER)]
            | fetch_scanblock_statement[set_command(self.state,
                COMMAND_FETCH_SCANBLOCK)]
            | close_statement[set_command(self.state, COMMAND_CLOSE)]
            | shutdown_statement[set_command(self.state, COMMAND_SHUTDOWN)]
            | shutdown_master_statement[set_command(self.state, COMMAND_SHUTDOWN_MASTER)]
            | status_statement[set_command(self.state, COMMAND_STATUS)]
            | drop_range_statement[set_command(self.state, COMMAND_DROP_RANGE)]
            | replay_start_statement[set_command(self.state,
                COMMAND_REPLAY_BEGIN)]
            | replay_log_statement[set_command(self.state, COMMAND_REPLAY_LOG)]
            | replay_commit_statement[set_command(self.state,
                COMMAND_REPLAY_COMMIT)]
            | exists_table_statement[set_command(self.state, COMMAND_EXISTS_TABLE)]
            | wait_for_maintenance_statement[set_command(self.state, COMMAND_WAIT_FOR_MAINTENANCE)]
            | balance_statement[set_command(self.state, COMMAND_BALANCE)]
            | heapcheck_statement[set_command(self.state, COMMAND_HEAPCHECK)]
            | compact_statement[set_command(self.state, COMMAND_COMPACT)]
            | stop_statement[set_command(self.state, COMMAND_STOP)]
            | set_statement[set_command(self.state, COMMAND_SET)]
            | rebuild_indices_statement[set_command(self.state, COMMAND_REBUILD_INDICES)]
            ;

          rebuild_indices_statement
            = REBUILD 
            >> *(index_type_spec[set_flags_index_type(self.state)])
            >> INDICES >> user_identifier[set_table_name(self.state)]
            ;

          index_type_spec
            = (VALUE | QUALIFIER)
            ;

          set_statement
            = SET >> set_variable_spec >> *(COMMA >> set_variable_spec )
            ;

          set_variable_spec
            = identifier[set_variable_name(self.state)] 
            >> '=' >> (TRUE | FALSE)[set_variable_value(self.state)]
            ;

          stop_statement
            = STOP >> user_identifier[set_rangeserver(self.state)]
            ;

          compact_statement
            = COMPACT >> *(compact_type_option)
                      >> TABLE >> user_identifier[set_table_name(self.state)]
                      >> *(string_literal[set_str(self.state)])
            | COMPACT >> *(compact_type_option) >> RANGES
                      >> (range_type[set_flags_range_type(self.state)]
                          >> *(PIPE >> range_type[set_flags_range_type(self.state)]))
            ;

          range_type
            = ALL
            | ROOT
            | METADATA
            | SYSTEM
            | USER
            ;

          compact_type_option
            = TYPE >> '=' >> compaction_type[set_flags_compaction_type(self.state)]
            ;

          compaction_type
            = MINOR
            | MAJOR
            | MERGING
            | GC
            ;

          heapcheck_statement
            = HEAPCHECK >> *(string_literal[set_output_file(self.state)])
            ;

          balance_statement
            = BALANCE >> !(ALGORITHM >> EQUAL >> user_identifier[set_balance_algorithm(self.state)])
              >> *(range_move_spec_list)
              >> *(balance_option_spec)
            ;

          range_move_spec_list
            = range_move_spec[add_range_move_spec(self.state)] >> *(COMMA
              >> range_move_spec[add_range_move_spec(self.state)])
            ;

          range_move_spec
            = LPAREN
            >> range_spec >> COMMA
            >> string_literal[set_source(self.state)] >> COMMA
            >> string_literal[set_destination(self.state)]
            >> RPAREN
            ;

          balance_option_spec
            = DURATION >> EQUAL >> uint_p[balance_set_duration(self.state)]
            ;

          drop_range_statement
            = DROP >> RANGE >> range_spec
            ;

          replay_start_statement
            = REPLAY >> START
            ;

          replay_log_statement
            = REPLAY >> LOG >> user_identifier[set_input_file(self.state)]
            ;

          replay_commit_statement
            = REPLAY >> COMMIT
            ;

          close_statement
            = CLOSE
            ;

          wait_for_maintenance_statement
            = WAIT >> FOR >> MAINTENANCE
            ;

          shutdown_statement
            = SHUTDOWN
            ;

          shutdown_master_statement
            = SHUTDOWN >> MASTER
            ;

          status_statement
            = STATUS
            ;

          fetch_scanblock_statement
            = FETCH >> SCANBLOCK >> !(lexeme_d[(+digit_p)[
                set_scanner_id(self.state)]])
            ;

          destroy_scanner_statement
            = DESTROY >> SCANNER >> !(lexeme_d[(+digit_p)[
                set_scanner_id(self.state)]])
            ;

          create_scanner_statement
            = CREATE >> SCANNER >> ON >> range_spec
              >> !where_clause
              >> *(option_spec)
            ;

          update_statement
            = UPDATE >> user_identifier[set_table_name(self.state)]
              >> user_identifier[set_input_file(self.state)]
            ;

          load_range_statement
            = LOAD >> RANGE >> range_spec >> !(REPLAY[set_replay(self.state)])
            ;

          dump_statement
	    = DUMP >> !(NOKEYS[set_nokeys(self.state)])
		   >> string_literal[set_output_file(self.state)]
            ;

          dump_table_option_spec
            = MAX_VERSIONS >> *EQUAL >> uint_p[scan_set_max_versions(self.state)]
            | BUCKETS >> uint_p[scan_set_buckets(self.state)]
            | REVS >> !EQUAL >> uint_p[scan_set_max_versions(self.state)]
            | INTO >> FILE >> string_literal[scan_set_outfile(self.state)]
            | NO_TIMESTAMPS[scan_clear_display_timestamps(self.state)]
            | FS >> EQUAL >> single_string_literal[set_field_separator(self.state)]
            ;

          dump_table_statement
	          = DUMP >> TABLE[scan_set_display_timestamps(self.state)]
                         >> table_identifier[set_table_name(self.state)]
            >> !(COLUMNS >> ('*' | (column_selection >> *(COMMA >> column_selection))))
		        >> !(dump_where_clause)
		        >> *(dump_table_option_spec)
            ;

          dump_pseudo_table_statement
            = DUMP >> PSEUDO >> TABLE >> table_identifier[set_table_name(self.state)]
                   >> INTO >> FILE >> string_literal[set_output_file(self.state)]
            ;

          range_spec
            = user_identifier[set_table_name(self.state)]
            >> LBRACK >> !(user_identifier[set_range_start_row(self.state)])
            >> DOTDOT
            >> (user_identifier | DOUBLEQUESTIONMARK)[
                set_range_end_row(self.state)] >> RBRACK
            ;

          drop_table_statement
            = DROP >> TABLE >> !(IF >> EXISTS[set_if_exists(self.state)])
              >> user_identifier[set_table_name(self.state)]
            ;
          rename_table_statement
            = RENAME >> TABLE
            >> user_identifier[set_table_name(self.state)]
            >> TO
            >> user_identifier[set_new_table_name(self.state)]
            ;

          alter_table_statement
            = ALTER >> TABLE >> user_identifier[start_alter_table(self.state)]
            >> ( WITH >> string_literal[set_input_file(self.state)] |
                 +(ADD >> create_definitions
                   | MODIFY[set_modify_flag(self.state, true)] >>
                   create_definitions[set_modify_flag(self.state, false)]
                   | drop_specification
                   | RENAME >> COLUMN >> FAMILY >> rename_column_definition) )
            ;

          exists_table_statement
            = EXISTS >> TABLE >> user_identifier[set_table_name(self.state)]
            ;

          get_listing_statement
            = (SHOW >> TABLES[set_tables_only(self.state)]) | (GET >> LISTING)
            ;

          delete_statement
            = DELETE >> delete_column_clause
              >> FROM >> user_identifier[set_table_name(self.state)]
              >> WHERE >> ROW >> EQUAL >> string_literal[
                  delete_set_row(self.state)]
              >> !(TIMESTAMP >> string_literal[set_delete_timestamp(self.state)]
                  | VERSION >> string_literal[set_delete_version_timestamp(self.state)])
            ;

          delete_column_clause
            = (STAR[delete_column(self.state)] | (column_name[
                delete_column(self.state)]
              >> *(COMMA >> column_name[delete_column(self.state)])))
            ;

          insert_statement
            = INSERT >> INTO >> user_identifier[set_table_name(self.state)]
              >> VALUES >> insert_value_list
            ;

          insert_value_list
            = insert_value[add_insert_value(self.state)] >> *(COMMA
              >> insert_value[add_insert_value(self.state)])
            ;

          insert_value
            =
              (
              LPAREN
              >> ( string_literal[set_insert_rowkey(self.state)] 
                 | identifier[set_insert_rowkey(self.state)] >>
                    parameter_list[set_insert_rowkey_call(self.state)] ) 
                >> COMMA 
              >> string_literal[set_insert_columnkey(self.state)] >> COMMA
              >> ( string_literal[set_insert_value(self.state)] 
                 | identifier[set_insert_value(self.state)] >>
                    parameter_list[set_insert_value_call(self.state)] ) 
                >> RPAREN
              | LPAREN
              >> string_literal[set_insert_timestamp(self.state)] >> COMMA
              >> ( string_literal[set_insert_rowkey(self.state)] 
                 | identifier[set_insert_rowkey(self.state)] >>
                    parameter_list[set_insert_rowkey_call(self.state)] ) 
                >> COMMA 
              >> string_literal[set_insert_columnkey(self.state)] >> COMMA
              >> ( string_literal[set_insert_value(self.state)] 
                 | identifier[set_insert_value(self.state)] >>
                    parameter_list[set_insert_value_call(self.state)] ) 
                >> RPAREN
              )
              ;

          show_statement
            = (SHOW >> CREATE >> TABLE >> user_identifier[set_table_name(self.state)])
            ;

          help_statement
            = (HELP | ESC_HELP | QUESTIONMARK) >> *anychar_p
            ;

          describe_table_statement
            = DESCRIBE >> TABLE >> !(WITH >> IDS[set_with_ids(self.state)])
                       >> user_identifier[set_table_name(self.state)]
            ;

          create_table_statement
            = CREATE >> TABLE
              >> user_identifier[start_create_table_statement(self.state)]
              >> ( LIKE >> user_identifier[set_clone_table_name(self.state)] |
                    WITH >> string_literal[set_input_file(self.state)] |
                   (create_definitions) >> *(table_option) )
            ;

          create_namespace_statement
            = CREATE >> (NAMESPACE | DATABASE)
              >> user_identifier[set_namespace(self.state)]
              >> !(IF >> NOT >> EXISTS[set_if_exists(self.state)])
            ;

          use_namespace_statement
            = USE >> user_identifier[set_namespace(self.state)]
            ;

          drop_namespace_statement
            = DROP >> (NAMESPACE | DATABASE) >> !(IF >> EXISTS[set_if_exists(self.state)])
              >> user_identifier[set_namespace(self.state)]
            ;

          table_option
            = GROUP_COMMIT_INTERVAL >> *EQUAL >> uint_p[set_group_commit_interval(self.state)]
            | access_group_option
            | column_option
            ;

          create_definitions
            = LPAREN >> create_definition
                     >> *(COMMA >> create_definition)
                     >> RPAREN
            ;

          create_definition
            = column_definition[close_column_family(self.state)]
              | access_group_definition[close_access_group(self.state)]
              | index_definition
            ;

          drop_specification
            = DROP >> LPAREN >> column_name[drop_column_family(self.state)]
                   >> *(COMMA >> column_name[drop_column_family(self.state)])
                   >> RPAREN
            | DROP >> *VALUE >> INDEX >> LPAREN
                   >> column_name[drop_value_index(self.state)]
                   >> *(COMMA >> column_name[drop_value_index(self.state)])
                   >> RPAREN
            | DROP >> QUALIFIER >> INDEX >> LPAREN
                   >> column_name[drop_qualifier_index(self.state)]
                   >> *(COMMA >> column_name[drop_qualifier_index(self.state)])
                   >> RPAREN
            ;

          rename_column_definition
            = LPAREN
              >> column_name[set_rename_column_family_old_name(self.state)] >> COMMA
              >> column_name[set_rename_column_family_new_name(self.state)]
              >> RPAREN
            ;

          modify_column_definitions
            = LPAREN
            >> modify_column_definition[close_column_family(self.state)]
            >> *(COMMA >> modify_column_definition[close_column_family(self.state)])
            >> RPAREN
            ;

          modify_column_definition
            = column_name[open_existing_column_family(self.state)] >> *(column_option)
            ;

          column_name
            = (identifier | string_literal)
            ;

          column_definition
            = column_name[open_column_family(self.state)] >> *(column_option)
            ;

          column_option
            = max_versions_option
            | time_order_option
            | ttl_option
            | counter_option
            ;

          max_versions_option
	        = (MAX_VERSIONS | REVS) >> *EQUAL
              >> lexeme_d[(+digit_p)[set_max_versions(self.state)]]
            ;

          time_order_option
            = TIME_ORDER >> ASC[set_time_order(self.state)]
            | TIME_ORDER >> DESC[set_time_order(self.state)]
            ;

          ttl_option
            = TTL >> *EQUAL >> duration[set_ttl(self.state)]
            ;

          counter_option
            = COUNTER >> boolean_literal[set_counter(self.state)]
            | COUNTER[set_counter(self.state)]
            ;

          duration
            = ureal_p >> !(MONTHS | MONTH | WEEKS | WEEK | DAYS | DAY | HOURS |
                HOUR | MINUTES | MINUTE | SECONDS | SECOND)
            ;

          access_group_definition
            = ACCESS >> GROUP
              >> user_identifier[open_access_group(self.state)]
              >> LPAREN 
              >> !(column_name[access_group_add_column_family(self.state)]
              >>   *(COMMA >> column_name[access_group_add_column_family(self.state)]))
              >> RPAREN
              >> *(access_group_option | column_option)
            ;

          index_definition
            = QUALIFIER >> INDEX
              >> user_identifier[create_qualifier_index(self.state)]
            | INDEX
              >> user_identifier[create_index(self.state)]
            ;

          access_group_option
            = in_memory_option
            | blocksize_option
            | replication_option
            | COMPRESSOR >> *EQUAL >> string_literal[
                set_compressor(self.state)]
            | bloom_filter_option
            ;

          bloom_filter_option
            = BLOOMFILTER >> *EQUAL
              >> string_literal[set_bloom_filter(self.state)]
            ;

          in_memory_option
            = IN_MEMORY >> boolean_literal[set_in_memory(self.state)]
            | IN_MEMORY[set_in_memory(self.state)]
            ;

          blocksize_option
            = BLOCKSIZE >> *EQUAL >> uint_p[
                set_blocksize(self.state)]
            ;

          replication_option
            = REPLICATION >> *EQUAL >> uint_p[
                set_replication(self.state)]
            ;

          select_statement
            = SELECT >> !(CELLS)
              >> ('*' | (column_selection >> *(COMMA >> column_selection)))
              >> FROM >> user_identifier[set_table_name(self.state)]
              >> !where_clause
              >> *(option_spec)
            ;

          column_selection
            = (identifier[scan_add_column_family(self.state)] >> QUALPREFIX >>
                        user_identifier[scan_add_column_qualifier(self.state, 
                            PREFIX_QUALIFIER)])
            | (identifier[scan_add_column_family(self.state)]
               >> COLON
               >> user_identifier[scan_add_column_qualifier(self.state)])
            | (identifier[scan_add_column_family(self.state)] 
               >> COLON
               >> STAR[scan_add_column_qualifier(self.state)])
            | (identifier[scan_add_column_family(self.state)] 
               >> COLON
               >> regexp_literal[scan_add_column_qualifier(self.state)])
            | (identifier[scan_add_column_family(self.state, NO_QUALIFIER)])
            ;

          where_clause
            = WHERE 
            >> where_predicate
            >> *(AND[scan_set_boolop(self.state, BOOLOP_AND)] >> where_predicate)
            ;

          dump_where_clause
            = WHERE
            >> dump_where_predicate
            >> *(AND[scan_set_boolop(self.state, BOOLOP_AND)] >> dump_where_predicate)
            ;

          relop
            = SW[scan_set_relop(self.state, RELOP_SW)]
            | EQUAL[scan_set_relop(self.state, RELOP_EQ)]
            | LE[scan_set_relop(self.state, RELOP_LE)]
            | LT[scan_set_relop(self.state, RELOP_LT)]
            | GE[scan_set_relop(self.state, RELOP_GE)]
            | GT[scan_set_relop(self.state, RELOP_GT)]
            ;

          time_predicate
            = !(string_literal[scan_set_time(self.state)] >> relop) >>
            TIMESTAMP >> relop >> string_literal[scan_set_time(self.state)]
            ;

          row_interval
            = !(string_literal[scan_set_row(self.state)] >> relop) >>
            ROW >> relop >> string_literal[scan_set_row(self.state)]
            ;

          row_predicate
            = row_interval[scan_add_row_interval(self.state)]
            | LPAREN >> row_interval[scan_add_row_interval(self.state)] >>
            *( OR >> row_interval[scan_add_row_interval(self.state)]) >> RPAREN
            | ROW >> REGEXP >> string_literal[scan_set_row_regexp(self.state)]
            ;

          cell_spec
            = string_literal[scan_set_cell_row(self.state)]
              >> COMMA >> string_literal[scan_set_cell_column(self.state)]
            ;

          cell_interval
            = !(cell_spec >> relop) >> CELL >> relop >> cell_spec
            ;

          cell_predicate
            = cell_interval
            | LPAREN >> cell_interval >> *( OR >> cell_interval ) >> RPAREN
            ;

          value_predicate
            = VALUE >> REGEXP >> string_literal[scan_set_value_regexp(self.state)]
            ;

          column_match
            = identifier[scan_set_column_predicate_name(self.state)]
            >> !column_qualifier_spec
            >> '='
            >> string_literal[scan_set_column_predicate_value(self.state, ColumnPredicate::EXACT_MATCH)]
            | identifier[scan_set_column_predicate_name(self.state)] 
            >> !column_qualifier_spec
            >> SW
            >> string_literal[scan_set_column_predicate_value(self.state, ColumnPredicate::PREFIX_MATCH)]
            | identifier[scan_set_column_predicate_name(self.state)] 
            >> !column_qualifier_spec
            >> RE
            >> regexp_literal[scan_set_column_predicate_value(self.state, ColumnPredicate::REGEX_MATCH)]
            | REGEXP >> LPAREN
            >> identifier[scan_set_column_predicate_name(self.state)]
            >> !column_qualifier_spec
            >> COMMA
            >> string_literal[scan_set_column_predicate_value(self.state, ColumnPredicate::REGEX_MATCH)]
            >> RPAREN
            | EXISTS >> LPAREN
            >> identifier[scan_set_column_predicate_name(self.state)]
            >> column_qualifier_spec
            >> RPAREN[scan_set_column_predicate_value(self.state)]
            ;

          column_qualifier_spec
            = COLON
            >> (user_identifier[scan_set_column_predicate_qualifier(self.state, ColumnPredicate::QUALIFIER_EXACT_MATCH)] |
                regexp_literal[scan_set_column_predicate_qualifier(self.state, ColumnPredicate::QUALIFIER_REGEX_MATCH)] |
                STAR[scan_set_column_predicate_qualifier(self.state, ColumnPredicate::QUALIFIER_PREFIX_MATCH)])
            ;

          column_predicate
            = column_match >> *( OR[scan_set_boolop(self.state, BOOLOP_OR)] >> column_match )
            ;

          where_predicate
            = cell_predicate
            | row_predicate
            | time_predicate
            | value_predicate
            | column_predicate
            ;

          dump_where_predicate
            = ROW >> REGEXP >> string_literal[scan_set_row_regexp(self.state)]
            | time_predicate
            | value_predicate
            ;

          option_spec
            = MAX_VERSIONS >> *EQUAL >> uint_p[scan_set_max_versions(self.state)]
            | REVS >> !EQUAL >> uint_p[scan_set_max_versions(self.state)]
            | LIMIT >> EQUAL >> uint_p[scan_set_row_limit(self.state)]
            | LIMIT >> uint_p[scan_set_row_limit(self.state)]
            | CELL_LIMIT >> EQUAL >> uint_p[scan_set_cell_limit(self.state)]
            | CELL_LIMIT >> uint_p[scan_set_cell_limit(self.state)]
            | CELL_LIMIT_PER_FAMILY >> EQUAL >> uint_p[scan_set_cell_limit_per_family(self.state)]
            | CELL_LIMIT_PER_FAMILY >> uint_p[scan_set_cell_limit_per_family(self.state)]
            | OFFSET >> EQUAL >> uint_p[scan_set_row_offset(self.state)]
            | OFFSET >> uint_p[scan_set_row_offset(self.state)]
            | CELL_OFFSET >> EQUAL >> uint_p[scan_set_cell_offset(self.state)]
            | CELL_OFFSET >> uint_p[scan_set_cell_offset(self.state)]
            | INTO >> FILE >> string_literal[scan_set_outfile(self.state)]
            | DISPLAY_TIMESTAMPS[scan_set_display_timestamps(self.state)]
            | DISPLAY_REVISIONS[scan_set_display_revisions(self.state)]
            | RETURN_DELETES[scan_set_return_deletes(self.state)]
            | KEYS_ONLY[scan_set_keys_only(self.state)]
            | NO_CACHE[scan_set_no_cache(self.state)]
            | NOESCAPE[set_noescape(self.state)]
            | NO_ESCAPE[set_noescape(self.state)]
            | SCAN_AND_FILTER_ROWS[scan_set_scan_and_filter_rows(self.state)]
            | FS >> EQUAL >> single_string_literal[set_field_separator(self.state)]
            | DEBUG >> single_string_literal[scan_set_debug(self.state)]
            ;

          unused_tokens
            = START_TIME
            | END_TIME
            | START_ROW
            | END_ROW
            | INCLUSIVE
            | EXCLUSIVE
            | STATS
            | STARTS
            ;

          datetime
            = date >> time
            ;

          uint_parser<unsigned int, 10, 2, 2> uint2_p;
          uint_parser<unsigned int, 10, 4, 4> uint4_p;

          date
            = lexeme_d[limit_d(0u, 9999u)[uint4_p[scan_set_year(self.state)]]
                >> '-'  //  Year
                >>  limit_d(1u, 12u)[uint2_p[scan_set_month(self.state)]]
                >> '-'  //  Month 01..12
                >>  limit_d(1u, 31u)[uint2_p[scan_set_day(self.state)]]
                        //  Day 01..31
                ];

          time
            = lexeme_d[limit_d(0u, 23u)[uint2_p[scan_set_hours(self.state)]]
                >> COLON //  Hours 00..23
                >>  limit_d(0u, 59u)[uint2_p[scan_set_minutes(self.state)]]
                >> COLON  //  Minutes 00..59
                >>  limit_d(0u, 59u)[uint2_p[scan_set_seconds(self.state)]]
                >>      //  Seconds 00..59
                !(real_p[scan_set_decimal_seconds(self.state)] |
                  COLON >> uint_p[scan_set_nanoseconds(self.state)])
                ];

          year
            = lexeme_d[limit_d(0u, 9999u)[uint4_p[scan_set_year(self.state)]]]
            ;

          load_data_statement
            = LOAD >> DATA >> INFILE
            >> !(load_data_option >> *(load_data_option))
            >> load_data_input
            >> INTO
            >> (TABLE >> user_identifier[set_table_name(self.state)]
                | FILE >> user_identifier[set_output_file(self.state)])
            ;

          load_data_input
            =  string_literal[set_input_file(self.state)]
            ;

          load_data_option
            = ROW_KEY_COLUMN >> EQUAL >> user_identifier[
                add_column(self.state)] >> *(PLUS >> user_identifier[
                add_column(self.state)])
            | TIMESTAMP_COLUMN >> EQUAL >> user_identifier[
                set_timestamp_column(self.state)]
            | HEADER_FILE >> EQUAL >> string_literal[
                set_header_file(self.state)]
            | ROW_UNIQUIFY_CHARS >> EQUAL >> uint_p[
                set_row_uniquify_chars(self.state)]
            | DUP_KEY_COLS >> EQUAL >> boolean_literal[
                set_dup_key_cols(self.state)]
            | DUP_KEY_COLS[set_dup_key_cols_true(self.state)]
            | DUPLICATE_KEY_COLUMNS[set_dup_key_cols_true(self.state)]
            | NOESCAPE[set_noescape(self.state)]
            | NO_ESCAPE[set_noescape(self.state)]
            | IGNORE_UNKNOWN_CFS[set_ignore_unknown_cfs(self.state)]
            | IGNORE_UNKNOWN_COLUMNS[set_ignore_unknown_cfs(self.state)]
            | SINGLE_CELL_FORMAT[set_single_cell_format(self.state)]
            | FS >> EQUAL >> single_string_literal[set_field_separator(self.state)]
            | NO_LOG[set_no_log(self.state)]
            ;

          /**
           * End grammar definition
           */

#ifdef BOOST_SPIRIT_DEBUG
          BOOST_SPIRIT_DEBUG_RULE(column_definition);
          BOOST_SPIRIT_DEBUG_RULE(column_name);
          BOOST_SPIRIT_DEBUG_RULE(column_option);
          BOOST_SPIRIT_DEBUG_RULE(column_match);
          BOOST_SPIRIT_DEBUG_RULE(column_predicate);
          BOOST_SPIRIT_DEBUG_RULE(column_qualifier_spec);
          BOOST_SPIRIT_DEBUG_RULE(column_selection);
          BOOST_SPIRIT_DEBUG_RULE(create_definition);
          BOOST_SPIRIT_DEBUG_RULE(create_definitions);
          BOOST_SPIRIT_DEBUG_RULE(drop_specification);
          BOOST_SPIRIT_DEBUG_RULE(rename_column_definition);
          BOOST_SPIRIT_DEBUG_RULE(modify_column_definitions);
          BOOST_SPIRIT_DEBUG_RULE(modify_column_definition);
          BOOST_SPIRIT_DEBUG_RULE(create_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(create_namespace_statement);
          BOOST_SPIRIT_DEBUG_RULE(use_namespace_statement);
          BOOST_SPIRIT_DEBUG_RULE(drop_namespace_statement);
          BOOST_SPIRIT_DEBUG_RULE(duration);
          BOOST_SPIRIT_DEBUG_RULE(identifier);
          BOOST_SPIRIT_DEBUG_RULE(user_identifier);
          BOOST_SPIRIT_DEBUG_RULE(max_versions_option);
          BOOST_SPIRIT_DEBUG_RULE(time_order_option);
          BOOST_SPIRIT_DEBUG_RULE(statement);
          BOOST_SPIRIT_DEBUG_RULE(string_literal);
          BOOST_SPIRIT_DEBUG_RULE(parameter_list);
          BOOST_SPIRIT_DEBUG_RULE(single_string_literal);
          BOOST_SPIRIT_DEBUG_RULE(double_string_literal);
          BOOST_SPIRIT_DEBUG_RULE(regexp_literal);
          BOOST_SPIRIT_DEBUG_RULE(ttl_option);
          BOOST_SPIRIT_DEBUG_RULE(counter_option);
          BOOST_SPIRIT_DEBUG_RULE(access_group_definition);
          BOOST_SPIRIT_DEBUG_RULE(index_definition);
          BOOST_SPIRIT_DEBUG_RULE(access_group_option);
          BOOST_SPIRIT_DEBUG_RULE(bloom_filter_option);
          BOOST_SPIRIT_DEBUG_RULE(in_memory_option);
          BOOST_SPIRIT_DEBUG_RULE(blocksize_option);
          BOOST_SPIRIT_DEBUG_RULE(replication_option);
          BOOST_SPIRIT_DEBUG_RULE(help_statement);
          BOOST_SPIRIT_DEBUG_RULE(describe_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(show_statement);
          BOOST_SPIRIT_DEBUG_RULE(select_statement);
          BOOST_SPIRIT_DEBUG_RULE(where_clause);
          BOOST_SPIRIT_DEBUG_RULE(where_predicate);
          BOOST_SPIRIT_DEBUG_RULE(time_predicate);
          BOOST_SPIRIT_DEBUG_RULE(cell_interval);
          BOOST_SPIRIT_DEBUG_RULE(cell_predicate);
          BOOST_SPIRIT_DEBUG_RULE(cell_spec);
          BOOST_SPIRIT_DEBUG_RULE(relop);
          BOOST_SPIRIT_DEBUG_RULE(row_interval);
          BOOST_SPIRIT_DEBUG_RULE(row_predicate);
          BOOST_SPIRIT_DEBUG_RULE(value_predicate);
          BOOST_SPIRIT_DEBUG_RULE(option_spec);
          BOOST_SPIRIT_DEBUG_RULE(unused_tokens);
          BOOST_SPIRIT_DEBUG_RULE(datetime);
          BOOST_SPIRIT_DEBUG_RULE(date);
          BOOST_SPIRIT_DEBUG_RULE(time);
          BOOST_SPIRIT_DEBUG_RULE(year);
          BOOST_SPIRIT_DEBUG_RULE(load_data_statement);
          BOOST_SPIRIT_DEBUG_RULE(load_data_input);
          BOOST_SPIRIT_DEBUG_RULE(load_data_option);
          BOOST_SPIRIT_DEBUG_RULE(insert_statement);
          BOOST_SPIRIT_DEBUG_RULE(insert_value_list);
          BOOST_SPIRIT_DEBUG_RULE(insert_value);
          BOOST_SPIRIT_DEBUG_RULE(delete_statement);
          BOOST_SPIRIT_DEBUG_RULE(delete_column_clause);
          BOOST_SPIRIT_DEBUG_RULE(table_option);
          BOOST_SPIRIT_DEBUG_RULE(get_listing_statement);
          BOOST_SPIRIT_DEBUG_RULE(drop_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(rename_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(alter_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(exists_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(load_range_statement);
          BOOST_SPIRIT_DEBUG_RULE(dump_statement);
          BOOST_SPIRIT_DEBUG_RULE(dump_where_clause);
          BOOST_SPIRIT_DEBUG_RULE(dump_where_predicate);
          BOOST_SPIRIT_DEBUG_RULE(dump_table_option_spec);
          BOOST_SPIRIT_DEBUG_RULE(dump_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(dump_pseudo_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(range_spec);
          BOOST_SPIRIT_DEBUG_RULE(update_statement);
          BOOST_SPIRIT_DEBUG_RULE(create_scanner_statement);
          BOOST_SPIRIT_DEBUG_RULE(destroy_scanner_statement);
          BOOST_SPIRIT_DEBUG_RULE(fetch_scanblock_statement);
          BOOST_SPIRIT_DEBUG_RULE(close_statement);
          BOOST_SPIRIT_DEBUG_RULE(shutdown_statement);
          BOOST_SPIRIT_DEBUG_RULE(shutdown_master_statement);
          BOOST_SPIRIT_DEBUG_RULE(status_statement);
          BOOST_SPIRIT_DEBUG_RULE(drop_range_statement);
          BOOST_SPIRIT_DEBUG_RULE(replay_start_statement);
          BOOST_SPIRIT_DEBUG_RULE(replay_log_statement);
          BOOST_SPIRIT_DEBUG_RULE(replay_commit_statement);
          BOOST_SPIRIT_DEBUG_RULE(balance_statement);
          BOOST_SPIRIT_DEBUG_RULE(balance_option_spec);
          BOOST_SPIRIT_DEBUG_RULE(range_move_spec_list);
          BOOST_SPIRIT_DEBUG_RULE(range_move_spec);
          BOOST_SPIRIT_DEBUG_RULE(heapcheck_statement);
          BOOST_SPIRIT_DEBUG_RULE(compact_statement);
          BOOST_SPIRIT_DEBUG_RULE(compact_type_option);
          BOOST_SPIRIT_DEBUG_RULE(compaction_type);
          BOOST_SPIRIT_DEBUG_RULE(metadata_sync_statement);
          BOOST_SPIRIT_DEBUG_RULE(metadata_sync_option_spec);
          BOOST_SPIRIT_DEBUG_RULE(stop_statement);
          BOOST_SPIRIT_DEBUG_RULE(range_type);
          BOOST_SPIRIT_DEBUG_RULE(table_identifier);
          BOOST_SPIRIT_DEBUG_RULE(pseudo_table_reference);
          BOOST_SPIRIT_DEBUG_RULE(dump_pseudo_table_statement);
          BOOST_SPIRIT_DEBUG_RULE(set_statement);
          BOOST_SPIRIT_DEBUG_RULE(set_variable_spec);
          BOOST_SPIRIT_DEBUG_RULE(rebuild_indices_statement);
          BOOST_SPIRIT_DEBUG_RULE(index_type_spec);
#endif
        }

        rule<ScannerT> const&
        start() const { return statement; }

        symbols<> keywords;

        rule<ScannerT> boolean_literal, column_definition, column_name,
          column_option, create_definition, create_definitions,
          drop_specification,
          rename_column_definition, create_table_statement, duration,
          modify_column_definitions, modify_column_definition,
          create_namespace_statement, use_namespace_statement, 
          drop_namespace_statement, identifier, user_identifier, 
          max_versions_option, time_order_option, statement,
          single_string_literal, double_string_literal, string_literal, 
          parameter_list, regexp_literal, ttl_option, counter_option, 
          access_group_definition, index_definition, access_group_option,
          bloom_filter_option, in_memory_option,
          blocksize_option, replication_option, help_statement,
          describe_table_statement, show_statement, select_statement,
          where_clause, where_predicate,
          time_predicate, relop, row_interval, row_predicate, column_match,
          column_predicate, column_qualifier_spec, value_predicate, column_selection,
          option_spec, unused_tokens, datetime, date, time, year,
          load_data_statement, load_data_input, load_data_option, insert_statement,
          insert_value_list, insert_value, delete_statement,
          delete_column_clause, table_option, table_option_in_memory,
          table_option_blocksize, table_option_replication, get_listing_statement,
          drop_table_statement, alter_table_statement,rename_table_statement,
          load_range_statement,
          dump_statement, dump_where_clause, dump_where_predicate,
          dump_table_statement, dump_table_option_spec, range_spec,
          exists_table_statement, update_statement, create_scanner_statement,
          destroy_scanner_statement, fetch_scanblock_statement,
          close_statement, shutdown_statement, shutdown_master_statement,
          drop_range_statement, replay_start_statement, replay_log_statement,
          replay_commit_statement, cell_interval, cell_predicate,
          cell_spec, wait_for_maintenance_statement, move_range_statement,
          balance_statement, range_move_spec_list, range_move_spec,
          balance_option_spec, heapcheck_statement, compact_statement,
          compact_type_option, compaction_type,
          metadata_sync_statement, metadata_sync_option_spec, stop_statement,
          range_type, table_identifier, pseudo_table_reference,
          dump_pseudo_table_statement, set_statement, set_variable_spec,
          rebuild_indices_statement, index_type_spec, status_statement;
      };

      ParserState &state;
    };
  }
}

#endif // Hypertable_Lib_HqlParser_h
