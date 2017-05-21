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

#include "LoadClient.h"

#ifdef HT_WITH_THRIFT
#include <ThriftBroker/Client.h>
#include <ThriftBroker/Config.h>
#endif

using namespace std;

LoadClient::LoadClient(const String &config_file, bool thrift)
  : m_thrift(thrift), m_native_client(0), m_ns(0), m_native_table(0),
    m_native_table_open(false), m_native_mutator(0), m_native_scanner(0) {
#ifdef HT_WITH_THRIFT
  m_thrift_namespace = 0;
  m_thrift_mutator = 0;
  m_thrift_scanner = 0;
#endif

  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    m_thrift_client.reset(new Thrift::Client("localhost", 15867));
    m_thrift_namespace = m_thrift_client->open_namespace("/");
#else
    HT_FATAL("Thrift support not installed");
#endif
  }
  else {
    m_native_client = std::make_shared<Hypertable::Client>(config_file);
    m_ns = m_native_client->open_namespace("/");
  }
}

LoadClient::LoadClient(bool thrift)
  : m_thrift(thrift), m_native_client(0), m_ns(0), m_native_table(0),
    m_native_table_open(false), m_native_mutator(0), m_native_scanner(0) {
#ifdef HT_WITH_THRIFT
  m_thrift_namespace = 0;
  m_thrift_mutator = 0;
  m_thrift_scanner = 0;
#endif

  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    m_thrift_client.reset(new Thrift::Client("localhost", 15867));
    m_thrift_namespace = m_thrift_client->open_namespace("/");
#else
    HT_FATAL("Thrift support not installed");
#endif
  }
  else {
    m_native_client = std::make_shared<Hypertable::Client>();
    m_ns = m_native_client->open_namespace("/");
  }
}

void
LoadClient::create_mutator(const String &tablename, int mutator_flags,
                           ::uint64_t shared_mutator_flush_interval)
{
  string ns, table;
  split_tablepath(tablename, &ns, &table);
  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    if (!ns.empty()) {
      m_thrift_client->namespace_close(m_thrift_namespace);
      m_thrift_namespace = m_thrift_client->namespace_open(ns);
    }
    m_thrift_mutator = m_thrift_client->open_mutator(m_thrift_namespace,
            table, mutator_flags, 0);
#endif
  }
  else {
    if (!m_native_table_open) {
      if (!ns.empty())
        m_ns = m_native_client->open_namespace(ns);
      m_native_table = m_ns->open_table(table);
      m_native_table_open = true;
    }
    m_native_mutator.reset(m_native_table->create_mutator(0, mutator_flags,
                                                          shared_mutator_flush_interval));
  }
}

void
LoadClient::set_cells(const Cells &cells)
{
  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    vector<ThriftGen::Cell> thrift_cells;
    for (const auto &cell : cells) {
      thrift_cells.push_back(ThriftGen::make_cell((const char*)cell.row_key,
          (const char*)cell.column_family,(const char*)cell.column_qualifier,
          string((const char*)cell.value, cell.value_len), cell.timestamp,
          cell.revision, (ThriftGen::KeyFlag::type) cell.flag));
    }
    m_thrift_client->mutator_set_cells(m_thrift_mutator, thrift_cells);
#endif
  }
  else {
    m_native_mutator->set_cells(cells);
  }
}

void
LoadClient::set_delete(const KeySpec &key) {
  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    vector<ThriftGen::Cell> thrift_cells;
    ThriftGen::KeyFlag::type flag = ThriftGen::KeyFlag::INSERT;

    if (key.column_family == 0 || *key.column_family == '\0')
      flag = ThriftGen::KeyFlag::DELETE_ROW;
    if (key.column_qualifier == 0 || *key.column_qualifier == '\0')
      flag = ThriftGen::KeyFlag::DELETE_CF;
    else
      flag = ThriftGen::KeyFlag::DELETE_CELL;

    thrift_cells.push_back(ThriftGen::make_cell((const char *)key.row,
        key.column_family, key.column_qualifier, std::string(""),
        key.timestamp, key.revision, flag));

    m_thrift_client->mutator_set_cells(m_thrift_mutator, thrift_cells);
#endif
  }
  else {
    m_native_mutator->set_delete(key);
  }
}

void
LoadClient::flush()
{
  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    m_thrift_client->flush_mutator(m_thrift_mutator);
#endif
  }
  else {
    m_native_mutator->flush();
  }
}

void
LoadClient::create_scanner(const String &tablename, const ScanSpec &scan_spec)
{
  string ns, table;
  split_tablepath(tablename, &ns, &table);
  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    if (!ns.empty()) {
      m_thrift_client->namespace_close(m_thrift_namespace);
      m_thrift_namespace = m_thrift_client->namespace_open(ns);
    }
    //copy scanspec column and first row interval
    ThriftGen::ScanSpec thrift_scan_spec;
    ThriftGen::RowInterval thrift_row_interval;
    thrift_row_interval.start_row = scan_spec.row_intervals[0].start;
    thrift_row_interval.end_row = scan_spec.row_intervals[0].end;
    thrift_row_interval.start_inclusive = scan_spec.row_intervals[0].start_inclusive;
    thrift_row_interval.end_inclusive = scan_spec.row_intervals[0].end_inclusive;
    thrift_row_interval.__isset.start_row = thrift_row_interval.__isset.end_row = true;
    thrift_row_interval.__isset.start_inclusive = thrift_row_interval.__isset.end_inclusive = true;

    thrift_scan_spec.columns.push_back(scan_spec.columns[0]);
    thrift_scan_spec.row_intervals.push_back(thrift_row_interval);
    thrift_scan_spec.__isset.columns = thrift_scan_spec.__isset.row_intervals = true;

    m_thrift_scanner = m_thrift_client->open_scanner(m_thrift_namespace,
            table, thrift_scan_spec);
#endif
  }
  else {
    if (!m_native_table_open) {
      if (!ns.empty())
        m_ns = m_native_client->open_namespace(ns);
      m_native_table = m_ns->open_table(table);
      m_native_table_open = true;
    }
    m_native_scanner.reset(m_native_table->create_scanner(scan_spec));
  }
}

uint64_t
LoadClient::get_all_cells()
{
  uint64_t bytes_scanned = 0;
  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    vector<ThriftGen::Cell> cells;

    do {
      m_thrift_client->next_cells(cells, m_thrift_scanner);
      for (const auto &cell : cells) {
        bytes_scanned += cell.key.row.size() + cell.key.column_family.size()
                         + cell.key.column_qualifier.size() + 8 + 8 + 2
                         + cell.value.size();
      }
    } while (cells.size());
#endif
    return bytes_scanned;
  }
  else {
    Cell cell;
    // see issue #802 why we can#t return m_native_scanner->bytes_scanned()
    while (m_native_scanner->next(cell)) {
      bytes_scanned += strlen(cell.row_key) + strlen(cell.column_family) +
                       8 + 8 + 2 + cell.value_len;
      if (cell.column_qualifier)
        bytes_scanned += strlen(cell.column_qualifier);
    }
    return bytes_scanned;
  }
}

void
LoadClient::close_scanner()
{
  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    m_thrift_client->close_scanner(m_thrift_scanner);
#endif
  }
  else {
    m_native_scanner = 0;
  }
}

LoadClient::~LoadClient()
{
  if (m_thrift) {
#ifdef HT_WITH_THRIFT
    m_thrift_client->close_namespace(m_thrift_namespace);
#endif
  }
}

void LoadClient::split_tablepath(const std::string &path, std::string *ns, std::string *table) {
  auto offset = path.find_last_of('/');
  ns->clear();
  if (offset == string::npos)
    *table = path;
  else {
    if (offset != 0) {
      if (!path.empty() && path[0] != '/')
        ns->append(1, '/');
      ns->append(path.substr(0, offset));
    }
    *table = path.substr(offset+1);
  }
}
