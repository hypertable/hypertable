/*
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

#include <Common/Compat.h>

#include "HqlInterpreter.h"

#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/HqlHelpText.h>
#include <Hypertable/Lib/HqlParser.h>
#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/LoadDataEscape.h>
#include <Hypertable/Lib/LoadDataFlags.h>
#include <Hypertable/Lib/LoadDataSource.h>
#include <Hypertable/Lib/LoadDataSourceFactory.h>
#include <Hypertable/Lib/Namespace.h>
#include <Hypertable/Lib/ScanSpec.h>
#include <Hypertable/Lib/Schema.h>
#include <Hypertable/Lib/TableSplit.h>

#include <FsBroker/Lib/FileDevice.h>

#include <Common/Config.h>
#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/ScopeGuard.h>
#include <Common/Status.h>
#include <Common/Stopwatch.h>
#include <Common/String.h>

#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/null.hpp>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iostream>

using namespace std;
using namespace Hypertable;
using namespace Hql;

namespace {

void close_file(int fd) {
  if (fd >= 0)
    close(fd);
}

int cmd_help(ParserState &state, HqlInterpreter::Callback &cb) {
  const char **text = HqlHelpText::get(state.str);

  if (text) {
    for (; *text; ++text)
      cb.on_return(*text);
  }
  else
    cb.on_return("\nno help for '" + state.str + "'");
  return 0;
}

int
cmd_create_namespace(Client *client, NamespacePtr &ns, ParserState &state,
    HqlInterpreter::Callback &cb) {

  if (!ns || state.ns.find('/') == 0)
    client->create_namespace(state.ns, NULL, false, state.if_exists);
  else
    client->create_namespace(state.ns, ns.get(), false, state.if_exists);
  cb.on_finish();
  return 0;
}

int
cmd_use_namespace(Client *client, NamespacePtr &ns, bool immutable_namespace,
                  ParserState &state, HqlInterpreter::Callback &cb) {

  if (ns && immutable_namespace)
    HT_THROW(Error::BAD_NAMESPACE, (String)"Attempting to modify immutable namespace " +
             ns->get_name() + " to " + state.ns);

  NamespacePtr tmp_ns;
  if (!ns || state.ns.find('/') == 0)
    tmp_ns = client->open_namespace(state.ns);
  else
    tmp_ns = client->open_namespace(state.ns, ns.get());
  if (tmp_ns)
    ns = tmp_ns;
  else
    HT_THROW(Error::NAMESPACE_DOES_NOT_EXIST, (String)"Couldn't open namespace " + state.ns);

  return 0;
}

int
cmd_drop_namespace(Client *client, NamespacePtr &ns, ParserState &state,
    HqlInterpreter::Callback &cb) {

  if (!ns || state.ns.find('/') == 0 )
    client->drop_namespace(state.ns, NULL, state.if_exists);
  else
    client->drop_namespace(state.ns, ns.get(), state.if_exists);
  cb.on_finish();
  return 0;
}

int
cmd_rename_table(NamespacePtr &ns, ParserState &state, HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");

  ns->rename_table(state.table_name, state.new_table_name);
  cb.on_finish();
  return 0;
}

int
cmd_exists_table(NamespacePtr &ns, ParserState &state, HqlInterpreter::Callback &cb) {
  string exists = (String)"true";

  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");

  if (!ns->exists_table(state.table_name))
    exists = (String)"false";
  cb.on_return(exists);
  cb.on_finish();
  return 0;
}

int
cmd_show_create_table(NamespacePtr &ns, ParserState &state,
                      HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  string schema_str = ns->get_schema_str(state.table_name, true);
  SchemaPtr schema( Schema::new_instance(schema_str) );
  string out_str = schema->render_hql(state.table_name);
  out_str += ";\n";
  cb.on_return(out_str);
  cb.on_finish();
  return 0;
}


int
cmd_create_table(NamespacePtr &ns, ParserState &state,
                 HqlInterpreter::Callback &cb) {
  if (!state.input_file.empty()) {
    if (state.input_file_src != LOCAL_FILE)
      HT_THROW(Error::SYNTAX_ERROR, "Schema file must reside in local FS");
    string schema_str;
    if (!FileUtils::read(state.input_file, schema_str))
      HT_THROW(Error::FILE_NOT_FOUND, state.input_file);
    state.create_schema.reset(Schema::new_instance(schema_str));
  }
  ns->create_table(state.table_name, state.create_schema);
  cb.on_finish();
  return 0;
}

int
cmd_alter_table(NamespacePtr &ns, ParserState &state,
                 HqlInterpreter::Callback &cb) {
  bool force {};

  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");

  if (!state.input_file.empty()) {
    if (state.input_file_src != LOCAL_FILE)
      HT_THROW(Error::SYNTAX_ERROR, "Schema file must reside in local FS");
    string schema_str;
    if (!FileUtils::read(state.input_file, schema_str))
      HT_THROW(Error::FILE_NOT_FOUND, state.input_file);
    state.alter_schema.reset(Schema::new_instance(schema_str));
    force = true;
  }

  ns->alter_table(state.table_name, state.alter_schema, force);
  ns->refresh_table(state.table_name);
  cb.on_finish();
  return 0;
}

int
cmd_compact(NamespacePtr &ns, ParserState &state,
                 HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  ns->compact(state.table_name, state.str, state.flags);
  cb.on_finish();
  return 0;
}


int
cmd_describe_table(NamespacePtr &ns, ParserState &state,
                   HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  string schema_str = ns->get_schema_str(state.table_name, state.with_ids);
  cb.on_return(schema_str);
  cb.on_finish();
  return 0;
}

int
cmd_select(NamespacePtr &ns, ConnectionManagerPtr &conn_manager,
           FsBroker::Lib::ClientPtr &fs_client, ParserState &state, HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  TablePtr table;
  boost::iostreams::filtering_ostream fout;
  FILE *outf = cb.output;
  int out_fd = -1;
  string localfs = "file://";
  char fs = state.field_separator ? state.field_separator : '\t';

  table = ns->open_table(state.table_name);
  TableScannerPtr scanner( table->create_scanner(state.scan.builder.get(), 0, true) );

  // whether it's select into file
  if (!state.scan.outfile.empty()) {
    FileUtils::expand_tilde(state.scan.outfile);

    if (boost::algorithm::ends_with(state.scan.outfile, ".gz"))
      fout.push(boost::iostreams::gzip_compressor());

    if (boost::algorithm::starts_with(state.scan.outfile, "dfs://") ||
        boost::algorithm::starts_with(state.scan.outfile, "fs://")) {
      // init Fs client if not done yet
      if (!fs_client)
        fs_client = std::make_shared<FsBroker::Lib::Client>(conn_manager, Config::properties);
      if (boost::algorithm::starts_with(state.scan.outfile, "dfs://"))
        fout.push(FsBroker::Lib::FileSink(fs_client, state.scan.outfile.substr(6)));
      else
        fout.push(FsBroker::Lib::FileSink(fs_client, state.scan.outfile.substr(5)));
    }
    else if (boost::algorithm::starts_with(state.scan.outfile, localfs))
      fout.push(boost::iostreams::file_descriptor_sink(state.scan.outfile.substr(localfs.size())));
    else
      fout.push(boost::iostreams::file_descriptor_sink(state.scan.outfile));

    fout << "#";
    if (state.scan.display_revisions)
      fout << "revision" << fs;
    if (state.scan.display_timestamps)
      fout << "timestamp" << fs;
    if (state.scan.keys_only)
      fout << "row\n";
    else
      fout << "row" << fs << "column" << fs << "value\n";
  }
  else if (!outf) {
    cb.on_scan(scanner);
    return 0;
  }
  else {
    out_fd = dup(fileno(outf));
    fout.push(boost::iostreams::file_descriptor_sink(out_fd));
  }

  HT_ON_SCOPE_EXIT(&close_file, out_fd);
  Cell cell;
  ::uint32_t nsec;
  time_t unix_time;
  struct tm tms;
  LoadDataEscape row_escaper;
  LoadDataEscape escaper;
  const char *unescaped_buf, *row_unescaped_buf;
  size_t unescaped_len, row_unescaped_len;

  if (fs != '\t') {
    row_escaper.set_field_separator(fs);
    escaper.set_field_separator(fs);
  }

  while (scanner->next(cell)) {
    if (cb.normal_mode) {
      // do some stats
      ++cb.total_cells;
      cb.total_keys_size += strlen(cell.row_key);

      if (cell.column_family && cell.column_qualifier)
        cb.total_keys_size += strlen(cell.column_qualifier) + 1;

      cb.total_values_size += cell.value_len;
    }
    if (state.scan.display_revisions) {
      if (cb.format_ts_in_nanos)
        fout << cell.revision << fs;
      else {
        nsec = cell.revision % 1000000000LL;
        unix_time = cell.revision / 1000000000LL;
        localtime_r(&unix_time, &tms);
        fout << Hypertable::format("%d-%02d-%02d %02d:%02d:%02d.%09d%c",
                        tms.tm_year + 1900, tms.tm_mon + 1, tms.tm_mday,
                        tms.tm_hour, tms.tm_min, tms.tm_sec, nsec, fs);
      }
    }
    if (state.scan.display_timestamps) {
      if (cb.format_ts_in_nanos)
        fout << cell.timestamp << fs;
      else {
        nsec = cell.timestamp % 1000000000LL;
        unix_time = cell.timestamp / 1000000000LL;
        localtime_r(&unix_time, &tms);
        fout << Hypertable::format("%d-%02d-%02d %02d:%02d:%02d.%09d%c", 
                        tms.tm_year + 1900, tms.tm_mon + 1, tms.tm_mday, 
                        tms.tm_hour, tms.tm_min, tms.tm_sec, nsec, fs);
      }
    }
    if (state.escape)
      row_escaper.escape(cell.row_key, strlen(cell.row_key),
             &row_unescaped_buf, &row_unescaped_len);
    else
      row_unescaped_buf = cell.row_key;
    if (!state.scan.keys_only) {
      if (cell.column_family && *cell.column_family) {
        fout << row_unescaped_buf << fs << cell.column_family;
        if (cell.column_qualifier && *cell.column_qualifier) {
          if (state.escape)
            escaper.escape(cell.column_qualifier, strlen(cell.column_qualifier),
                &unescaped_buf, &unescaped_len);
          else
            unescaped_buf = cell.column_qualifier;
          fout << ":" << unescaped_buf;
        }

      }
      else
        fout << row_unescaped_buf;

      if (state.escape)
        escaper.escape((const char *)cell.value, (size_t)cell.value_len,
                       &unescaped_buf, &unescaped_len);
      else {
        unescaped_buf = (const char *)cell.value;
        unescaped_len = (size_t)cell.value_len;
      }

      fout << fs;
      switch(cell.flag) {
      case FLAG_INSERT:
        fout.write(unescaped_buf, unescaped_len);
        fout << "\n";
        break;
      case FLAG_DELETE_ROW:
        fout << fs << "DELETE ROW\n";
        break;
       case FLAG_DELETE_COLUMN_FAMILY:
        fout.write(unescaped_buf, unescaped_len);
        fout << fs << "DELETE COLUMN FAMILY\n";
        break;
      case FLAG_DELETE_CELL:
        fout.write(unescaped_buf, unescaped_len);
        fout << fs << "DELETE CELL\n";
        break;
      case FLAG_DELETE_CELL_VERSION:
        fout.write(unescaped_buf, unescaped_len);
        fout << fs << "DELETE CELL VERSION\n";
        break;
      default:
        fout << fs << "BAD KEY FLAG\n";
      }
    }
    else
      fout << row_unescaped_buf << "\n";
  }

  fout.strict_sync();

  cb.on_finish(scanner);
  return 0;
}


int
cmd_dump_table(NamespacePtr &ns,
               ConnectionManagerPtr &conn_manager, FsBroker::Lib::ClientPtr &fs_client,
               ParserState &state, HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  TablePtr table;
  boost::iostreams::filtering_ostream fout;
  FILE *outf = cb.output;
  int out_fd = -1;
  string localfs = "file://";
  char fs = state.field_separator ? state.field_separator : '\t';

  // verify parameters

  TableDumperPtr dumper = std::make_shared<TableDumper>(ns, state.table_name, state.scan.builder.get());

  // whether it's select into file
  if (!state.scan.outfile.empty()) {
    FileUtils::expand_tilde(state.scan.outfile);

    if (boost::algorithm::ends_with(state.scan.outfile, ".gz"))
      fout.push(boost::iostreams::gzip_compressor());
  
    if (boost::algorithm::starts_with(state.scan.outfile, "dfs://") ||
        boost::algorithm::starts_with(state.scan.outfile, "fs://")) {
      // init Fs client if not done yet
      if (!fs_client)
        fs_client = std::make_shared<FsBroker::Lib::Client>(conn_manager, Config::properties);
      if (boost::algorithm::starts_with(state.scan.outfile, "dfs://"))
        fout.push(FsBroker::Lib::FileSink(fs_client, state.scan.outfile.substr(6)));
      else
        fout.push(FsBroker::Lib::FileSink(fs_client, state.scan.outfile.substr(5)));
    }
    else if (boost::algorithm::starts_with(state.scan.outfile, localfs))
      fout.push(boost::iostreams::file_descriptor_sink(state.scan.outfile.substr(localfs.size())));
    else
      fout.push(boost::iostreams::file_descriptor_sink(state.scan.outfile));

    if (state.scan.display_timestamps)
      fout << "#timestamp" << fs << "row" << fs << "column" << fs << "value\n";
    else
      fout << "#row" << fs << "column" << fs << "value\n";
  }
  else if (!outf) {
    cb.on_dump(*dumper.get());
    return 0;
  }
  else {
    out_fd = dup(fileno(outf));
    fout.push(boost::iostreams::file_descriptor_sink(out_fd));
  }

  HT_ON_SCOPE_EXIT(&close_file, out_fd);
  Cell cell;
  LoadDataEscape row_escaper;
  LoadDataEscape escaper;
  const char *unescaped_buf, *row_unescaped_buf;
  size_t unescaped_len, row_unescaped_len;

  if (fs != '\t') {
    row_escaper.set_field_separator(fs);
    escaper.set_field_separator(fs);
  }

  while (dumper->next(cell)) {
    if (cb.normal_mode) {
      // do some stats
      ++cb.total_cells;
      cb.total_keys_size += strlen(cell.row_key);

      if (cell.column_family && cell.column_qualifier)
        cb.total_keys_size += strlen(cell.column_qualifier) + 1;

      cb.total_values_size += cell.value_len;
    }

    if (state.scan.display_timestamps)
      fout << cell.timestamp << fs;

    if (state.escape)
      row_escaper.escape(cell.row_key, strlen(cell.row_key),
             &row_unescaped_buf, &row_unescaped_len);
    else
      row_unescaped_buf = cell.row_key;

    if (cell.column_family) {
      fout << row_unescaped_buf << fs << cell.column_family;
      if (cell.column_qualifier && *cell.column_qualifier) {
        if (state.escape)
          escaper.escape(cell.column_qualifier, strlen(cell.column_qualifier),
                   &unescaped_buf, &unescaped_len);
        else
          unescaped_buf = cell.column_qualifier;
        fout << ":" << unescaped_buf;
      }
    }
    else
      fout << row_unescaped_buf;

    if (state.escape)
      escaper.escape((const char *)cell.value, (size_t)cell.value_len,
             &unescaped_buf, &unescaped_len);
    else {
      unescaped_buf = (const char *)cell.value;
      unescaped_len = (size_t)cell.value_len;
    }

    HT_ASSERT(cell.flag == FLAG_INSERT);

    fout << fs ;
    fout.write(unescaped_buf, unescaped_len);
    fout << "\n";
  }

  fout.strict_sync();

  cb.on_finish();
  return 0;
}

int
cmd_load_data(NamespacePtr &ns, ::uint32_t mutator_flags,
              ConnectionManagerPtr &conn_manager, 
              FsBroker::Lib::ClientPtr &fs_client,
              ParserState &state, HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  TablePtr table;
  TableMutatorPtr mutator;
  bool into_table = true;
  bool display_timestamps = false;
  boost::iostreams::filtering_ostream fout;
  FILE *outf = cb.output;
  int out_fd = -1;
  bool largefile_mode = false;
  ::uint64_t running_total = 0;
  ::uint64_t consume_threshold = 0;
  bool ignore_unknown_columns = false;
  char fs = state.field_separator ? state.field_separator : '\t';

  if (LoadDataFlags::ignore_unknown_cfs(state.load_flags))
    ignore_unknown_columns = true;

  // Turn on no-log-sync unconditionally for LOAD DATA INFILE
  if (LoadDataFlags::no_log(state.load_flags))
    mutator_flags |= Table::MUTATOR_FLAG_NO_LOG;
  else
    mutator_flags |= Table::MUTATOR_FLAG_NO_LOG_SYNC;

  if (state.table_name.empty()) {
    if (state.output_file.empty())
      HT_THROW(Error::HQL_PARSE_ERROR,
          "LOAD DATA INFILE ... INTO FILE - bad filename");
    fout.push(boost::iostreams::file_descriptor_sink(state.output_file));
    into_table = false;
  }
  else {
    if (outf) {
      out_fd = dup(fileno(outf));
      fout.push(boost::iostreams::file_descriptor_sink(out_fd));
    }
    else
      fout.push(boost::iostreams::null_sink());
    table = ns->open_table(state.table_name);
    mutator.reset(table->create_mutator(0, mutator_flags));
  }

  HT_ON_SCOPE_EXIT(&close_file, out_fd);


  bool is_delete;

  // init Fs client if not done yet
  if(state.input_file_src == DFS_FILE && !fs_client)
    fs_client = std::make_shared<FsBroker::Lib::Client>(conn_manager, Config::properties);

  LoadDataSourcePtr lds(LoadDataSourceFactory::create(fs_client, state.input_file,
                                                      state.input_file_src, state.header_file, state.header_file_src,
                                                      state.columns, state.timestamp_column, fs,
                                                      state.row_uniquify_chars, state.load_flags));

  cb.file_size = lds->get_source_size();
  if (cb.file_size > std::numeric_limits<unsigned long>::max()) {
    largefile_mode = true;
    unsigned long adjusted_size = (unsigned long)(cb.file_size / 1048576LL);
    consume_threshold = 1048576LL;
    cb.on_update(adjusted_size);
  }
  else
    cb.on_update(cb.file_size);

  if (!into_table) {
    display_timestamps = lds->has_timestamps();
    if (display_timestamps)
      fout << "timestamp" << fs << "row" << fs << "column" << fs << "value\n";
    else
      fout << "row" << fs << "column" << fs << "value\n";
  }

  KeySpec key;
  ::uint8_t *value;
  ::uint32_t value_len;
  ::uint32_t consumed = 0;
  LoadDataEscape row_escaper;
  LoadDataEscape qualifier_escaper;
  LoadDataEscape value_escaper;
  const char *escaped_buf;
  size_t escaped_len;

  if (fs != '\t') {
    row_escaper.set_field_separator(fs);
    qualifier_escaper.set_field_separator(fs);
    value_escaper.set_field_separator(fs);
  }

  try {

    while (lds->next(&key, &value, &value_len, &is_delete, &consumed)) {

      ++cb.total_cells;
      cb.total_values_size += value_len;
      cb.total_keys_size += key.row_len;

      if (state.escape) {
        row_escaper.unescape((const char *)key.row, (size_t)key.row_len,
            &escaped_buf, &escaped_len);
        key.row = escaped_buf;
        key.row_len = escaped_len;
        qualifier_escaper.unescape(key.column_qualifier, 
                (size_t)key.column_qualifier_len, &escaped_buf, &escaped_len);
        key.column_qualifier = escaped_buf;
        key.column_qualifier_len = escaped_len;
        value_escaper.unescape((const char *)value, 
                (size_t)value_len, &escaped_buf, &escaped_len);
      }
      else {
        escaped_buf = (const char *)value;
        escaped_len = (size_t)value_len;
      }

      if (into_table) {
        try {
          bool skip = false;
          if (ignore_unknown_columns) {
            SchemaPtr schema = table->schema();
            if (!schema->get_column_family(key.column_family))
              skip = true;
          }
          if (!skip) {
            if (is_delete)
              mutator->set_delete(key);
            else
              mutator->set(key, escaped_buf, escaped_len);
          }
        }
        catch (Exception &e) {
          do {
            mutator->show_failed(e);
          } while (!mutator->retry());
        }
      }
      else {
        if (display_timestamps)
          fout << key.timestamp << fs << key.row << fs
               << key.column_family << fs << escaped_buf << "\n";
        else
          fout << key.row << fs << key.column_family << fs
               << escaped_buf << "\n";
      }

      if (cb.normal_mode && state.input_file_src != STDIN) {
        if (largefile_mode == true) {
          running_total += consumed;
          if (running_total >= consume_threshold) {
            consumed = 1 + (unsigned long)((running_total - consume_threshold) 
                    / 1048576LL);
            consume_threshold += (::uint64_t)consumed * 1048576LL;
            cb.on_progress(consumed);
          }
        }
        else
          cb.on_progress(consumed);
      }
    }
  }
  catch (Exception &e) {
    HT_THROW2F(e.code(), e, "line number %lld", (Lld)lds->get_current_lineno());
  }

  fout.strict_sync();

  cb.on_finish(mutator);
  return 0;
}

int
cmd_insert(NamespacePtr &ns, ParserState &state, HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  TablePtr table;
  const Cells &cells = state.inserts.get();

  table = ns->open_table(state.table_name);
  TableMutatorPtr mutator( table->create_mutator() );

  try {
    mutator->set_cells(cells);
  }
  catch (Exception &e) {
    do {
      mutator->show_failed(e);
    } while (!mutator->retry());

    if (mutator->get_last_error())
      HT_THROW(mutator->get_last_error(),
              Error::get_text(mutator->get_last_error()));
  }
  if (cb.normal_mode) {
    cb.total_cells = cells.size();

    for (const auto &cell : cells) {
      cb.total_keys_size += cell.column_qualifier
          ? (strlen(cell.column_qualifier) + 1) : 0;
      cb.total_values_size += cell.value_len;
    }
  }

  // flush the mutator
  cb.on_finish(mutator);

  // report errors during mutator->flush
  if (mutator->get_last_error())
    HT_THROW(mutator->get_last_error(),
            Error::get_text(mutator->get_last_error()));
  return 0;
}

int
cmd_delete(NamespacePtr &ns, ParserState &state, HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  TablePtr table;
  KeySpec key;
  char *column_qualifier;

  table = ns->open_table(state.table_name);
  TableMutatorPtr mutator(table->create_mutator());

  key.row = state.delete_row.c_str();
  key.row_len = state.delete_row.length();

  if (state.delete_version_time) {
    key.flag = FLAG_DELETE_CELL_VERSION;
    key.timestamp = state.delete_version_time;
  }
  else if (state.delete_time) {
    key.flag = FLAG_DELETE_CELL;
    key.timestamp = state.delete_time;
  }
  else
    key.timestamp = AUTO_ASSIGN;

  if (state.delete_all_columns) {
    try {
      key.flag = FLAG_DELETE_ROW;
      mutator->set_delete(key);
    }
    catch (Exception &e) {
      mutator->show_failed(e);
      return 2;
    }
  }
  else {
    for (const auto &col : state.delete_columns) {
      ++cb.total_cells;

      key.column_family = col.c_str();
      if ((column_qualifier = (char*)strchr(col.c_str(), ':')) != 0) {
        *column_qualifier++ = 0;
        key.column_qualifier = column_qualifier;
        key.column_qualifier_len = strlen(column_qualifier);
        if (key.flag == FLAG_INSERT)
          key.flag = FLAG_DELETE_CELL;
      }
      else {
        key.column_qualifier = 0;
        key.column_qualifier_len = 0;
        if (key.flag == FLAG_INSERT)
          key.flag = FLAG_DELETE_COLUMN_FAMILY;
      }
      try {
        mutator->set_delete(key);
      }
      catch (Exception &e) {
        mutator->show_failed(e);
        return 2;
      }
    }
  }

  cb.on_finish(mutator);
  return 0;
}

int
cmd_get_listing(NamespacePtr &ns, ParserState &state,
    HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");
  std::vector<NamespaceListing> listing;
  ns->get_listing(false, listing);
  for (const auto &entry : listing) {
    if (entry.is_namespace && !state.tables_only)
      cb.on_return(entry.name + "\t(namespace)");
    else if (!entry.is_namespace)
      cb.on_return(entry.name);
  }
  cb.on_finish();
  return 0;
}

int
cmd_drop_table(NamespacePtr &ns, ParserState &state,
               HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");

  ns->drop_table(state.table_name, state.if_exists);
  cb.on_finish();
  return 0;
}

int cmd_balance(Client *client, ParserState &state,
            HqlInterpreter::Callback &cb) {
  Lib::Master::ClientPtr master = client->get_master_client();

  master->balance(state.balance_plan);

  cb.on_finish();
  return 0;
}

int cmd_stop(Client *client, ParserState &state, HqlInterpreter::Callback &cb) {
  Lib::Master::ClientPtr master = client->get_master_client();
  master->stop(state.rs_name);
  cb.on_finish();
  return 0;
}

int cmd_set(Client *client, ParserState &state, HqlInterpreter::Callback &cb) {
  Lib::Master::ClientPtr master = client->get_master_client();
  master->set_state(state.variable_specs);
  cb.on_finish();
  return 0;
}

int cmd_status(Client *client, ParserState &state, HqlInterpreter::Callback &cb) {
  Status status;
  Timer timer(10000, true);
  int error {};

  try {
    error = client->get_hyperspace_session()->status(status, &timer);
  }
  catch (Exception &e) {
    cb.on_return(Hypertable::format("Hypertable CRITICAL - %s - %s",
                                    Error::get_text(e.code()), e.what()));
    return 2;
  }

  if (error != Error::OK) {
    cb.on_return(Hypertable::format("Hypertable CRITICAL - %s",
                                    Error::get_text(error)));
    return 2;
  }

  if (status.get() != Status::Code::OK) {
    Status::Code code;
    string text;
    status.get(&code, text);
    cb.on_return(Hypertable::format("Hypertable %s - %s",
                                    Status::code_to_string(code), text.c_str()));
    return static_cast<int>(code);
  }

  Lib::Master::ClientPtr master = client->get_master_client();
  master->system_status(status, &timer);

  if (status.get() != Status::Code::OK) {
    Status::Code code;
    string text;
    status.get(&code, text);
    cb.on_return(Hypertable::format("Hypertable %s - %s",
                                    Status::code_to_string(code), text.c_str()));
    return static_cast<int>(code);
  }

  cb.on_return("Hypertable OK");
  return 0;
}

int
cmd_rebuild_indices(Client *client, NamespacePtr &ns, ParserState &state,
                    HqlInterpreter::Callback &cb) {
  if (!ns)
    HT_THROW(Error::BAD_NAMESPACE, "Null namespace");

  if (state.table_name.empty())
    HT_THROW(Error::HQL_PARSE_ERROR, "Empty table name");

  NamespacePtr working_ns = ns;
  string table_basename = state.table_name;
  size_t lastslash = state.table_name.find_last_of('/');
  if (lastslash != string::npos) {
    table_basename = state.table_name.substr(lastslash+1);
    if (state.table_name[0] == '/') {
      if (lastslash == 0)
        working_ns = client->open_namespace("/");
      else
        working_ns =
          client->open_namespace(state.table_name.substr(0, lastslash));
    }
    else
      working_ns = client->open_namespace(state.table_name.substr(0, lastslash),
                                          ns.get());
  }

  int8_t parts = state.flags ? static_cast<int8_t>(state.flags) : TableParts::ALL;
  TableParts table_parts(parts);

  working_ns->rebuild_indices(table_basename, table_parts);

  cb.on_finish();
  return 0;
}


int cmd_shutdown_master(Client *client, HqlInterpreter::Callback &cb) {
  client->shutdown();
  cb.on_finish();
  return 0;
}

int cmd_close(Client *client, HqlInterpreter::Callback &cb) {
  client->close();
  cb.on_finish();
  return 0;
}

} // local namespace


HqlInterpreter::HqlInterpreter(Client *client, ConnectionManagerPtr &conn_manager,
    bool immutable_namespace) : m_client(client), m_mutator_flags(0),
    m_conn_manager(conn_manager), m_fs_client(0), m_immutable_namespace(immutable_namespace) {
  if (Config::properties->get_bool("Hypertable.HqlInterpreter.Mutator.NoLogSync"))
    m_mutator_flags = Table::MUTATOR_FLAG_NO_LOG_SYNC;

}

void HqlInterpreter::set_namespace(const string &ns) {
  if (m_namespace && m_immutable_namespace)
    HT_THROW(Error::BAD_NAMESPACE, (String)"Attempting to modify immutable namespace " +
             m_namespace->get_name() + " to " + ns);
  m_namespace = m_client->open_namespace(ns);
}

int HqlInterpreter::execute(const string &line, Callback &cb) {
  ParserState state(m_namespace);
  string stripped_line = line;

  boost::trim(stripped_line);

  Hql::Parser parser(state);
  parse_info<> info = parse(stripped_line.c_str(), parser, space_p);

  if (info.full) {
    cb.on_parsed(state);

    switch (state.command) {
    case COMMAND_SHOW_CREATE_TABLE:
      return cmd_show_create_table(m_namespace, state, cb);
    case COMMAND_HELP:
      return cmd_help(state, cb);
    case COMMAND_EXISTS_TABLE:
      return cmd_exists_table(m_namespace, state, cb);
    case COMMAND_CREATE_TABLE:
      return cmd_create_table(m_namespace, state, cb);
    case COMMAND_DESCRIBE_TABLE:
      return cmd_describe_table(m_namespace, state, cb);
    case COMMAND_SELECT:
      return cmd_select(m_namespace, m_conn_manager, m_fs_client,
                        state, cb);
    case COMMAND_LOAD_DATA:
      return cmd_load_data(m_namespace, m_mutator_flags,
                           m_conn_manager, m_fs_client, state, cb);
    case COMMAND_INSERT:
      return cmd_insert(m_namespace, state, cb);
    case COMMAND_DELETE:
      return cmd_delete(m_namespace, state, cb);
    case COMMAND_GET_LISTING:
      return cmd_get_listing(m_namespace, state, cb);
    case COMMAND_ALTER_TABLE:
      return cmd_alter_table(m_namespace, state, cb);
    case COMMAND_COMPACT:
      return cmd_compact(m_namespace, state, cb);
    case COMMAND_DROP_TABLE:
      return cmd_drop_table(m_namespace, state, cb);
    case COMMAND_RENAME_TABLE:
      return cmd_rename_table(m_namespace, state, cb);
    case COMMAND_DUMP_TABLE:
      return cmd_dump_table(m_namespace, m_conn_manager, m_fs_client,
                            state, cb);
    case COMMAND_CLOSE:
      return cmd_close(m_client, cb);
    case COMMAND_SHUTDOWN_MASTER:
      return cmd_shutdown_master(m_client, cb);
    case COMMAND_CREATE_NAMESPACE:
      return cmd_create_namespace(m_client, m_namespace, state, cb);
    case COMMAND_USE_NAMESPACE:
      return cmd_use_namespace(m_client, m_namespace,
                               m_immutable_namespace, state, cb);
    case COMMAND_DROP_NAMESPACE:
      return cmd_drop_namespace(m_client, m_namespace, state, cb);
    case COMMAND_BALANCE:
      return cmd_balance(m_client, state, cb);
    case COMMAND_STOP:
      return cmd_stop(m_client, state, cb);
    case COMMAND_SET:
      return cmd_set(m_client, state, cb);
    case COMMAND_STATUS:
      return cmd_status(m_client, state, cb);
    case COMMAND_REBUILD_INDICES:
      return cmd_rebuild_indices(m_client, m_namespace, state, cb);

    default:
      HT_THROW(Error::HQL_PARSE_ERROR, String("unsupported command: ") + stripped_line);
    }
  }
  else
    HT_THROW(Error::HQL_PARSE_ERROR, String("parse error at: ") + info.stop + " (" + stripped_line + ")");
  return 0;
}
