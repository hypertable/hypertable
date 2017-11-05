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
#include "TableScannerAsync.h"

#include <Hypertable/Lib/Table.h>
#include <Hypertable/Lib/IndexScannerCallback.h>
#include <Hypertable/Lib/LoadDataEscape.h>

#include <Common/Error.h>
#include <Common/Regex.h>
#include <Common/String.h>

#include <chrono>
#include <cctype>

using namespace Hypertable;
using namespace std;

/**
 *
 */
TableScannerAsync::TableScannerAsync(Comm *comm, 
      ApplicationQueueInterfacePtr &app_queue, Table *table,
      RangeLocatorPtr &range_locator, const ScanSpec &scan_spec, 
      uint32_t timeout_ms, ResultCallback *cb, int flags)
  : m_error(Error::OK), m_stopwatch(true)
{
  unique_lock<mutex> lock(m_mutex);
  ScanSpecBuilder primary_spec(scan_spec);
  ScanSpecBuilder index_spec;
  const ScanSpec *first_pass_spec;
  bool use_qualifier = false;
  bool row_intervals_applied = false;
  std::vector<CellPredicate> cell_predicates;

  HT_ASSERT(timeout_ms);

  // can we optimize this query with an index?
  if (!(flags & Table::SCANNER_FLAG_IGNORE_INDEX)
      && use_index(table, scan_spec, index_spec,
                   cell_predicates,
                   &use_qualifier,
                   &row_intervals_applied)) {

    first_pass_spec = &index_spec.get();

    m_use_index = true;

    // create a ResultCallback object which will load the keys from
    // the index, then sort and verify them
    cb = new IndexScannerCallback(this, table, primary_spec.get(), cell_predicates,
                                  cb, timeout_ms, use_qualifier, row_intervals_applied);

    // get the index table
    if (use_qualifier)
      table = table->get_qualifier_index_table().get();
    else
      table = table->get_index_table().get();

    // fall through and retrieve values from the primary table. the 
    // IndexScannerCallback object will then transform the results and forward 
    // them to the original callback
  }
  else {
    transform_primary_scan_spec(primary_spec);
    first_pass_spec = &primary_spec.get();
  }

  m_cb = cb;
  m_table = table;

  init(comm, app_queue, table, range_locator, *first_pass_spec, timeout_ms, cb);
}


bool TableScannerAsync::use_index(Table *table, const ScanSpec &primary_spec, 
                                  ScanSpecBuilder &index_spec,
                                  std::vector<CellPredicate> &cell_predicates,
                                  bool *use_qualifier,
                                  bool *row_intervals_applied)
{
  if (!table->has_index_table() && !table->has_qualifier_index_table())
    return false;

  index_spec.set_keys_only(true);
  index_spec.add_column("v1");

  cell_predicates.resize(256);

  string family;
  DynamicBuffer regex_prefix_buf;
  const char *prefix;
  size_t prefix_len;
  ColumnFamilySpec *cf_spec = 0;
  string index_row_prefix;
  DynamicBuffer index_row_buf;
  LoadDataEscape lde;
  bool qualifier_match_only = false;
  bool value_match = false;
  size_t qualifier_index_count = 0;
  size_t value_index_count = 0;
  size_t row_intervals_applied_count = 0;

  // for value prefix queries we require normal indicies for ALL scanned columns
  if (!primary_spec.column_predicates.empty()) {
    size_t id = 0;

    for (const auto &cp : primary_spec.column_predicates) {

      if ((cf_spec = table->schema()->get_column_family(cp.column_family)) == 0)
        return false;

      if (cf_spec->get_value_index())
        value_index_count++;
      if (cf_spec->get_qualifier_index())
        qualifier_index_count++;

      // Make sure that all prediates match against the value index, or all
      // predicates match against the qualifier index
      if (cp.operation & ColumnPredicate::VALUE_MATCH) {
        value_match = true;
        if (qualifier_match_only)
          return false;
      }
      else if (cp.operation & ColumnPredicate::QUALIFIER_MATCH) {
        qualifier_match_only = true;
        if (value_match)
          return false;
      }

      UInt8Formatter cfid(cf_spec->get_id());

      if (cp.operation & ColumnPredicate::REGEX_MATCH) {
        if (!Regex::extract_prefix((const char *)cp.value, (size_t)cp.value_len,
                                   &prefix, &prefix_len, regex_prefix_buf))
          return false;
        const char *escaped_prefix;
        size_t escaped_prefix_len;
        lde.escape(prefix, prefix_len,
                   &escaped_prefix, &escaped_prefix_len);
        if (index_row_buf.size < escaped_prefix_len+5)
          index_row_buf.grow(escaped_prefix_len+5);
        index_row_buf.ptr = (uint8_t*)cfid.append_to((char*)index_row_buf.base);
        *index_row_buf.ptr++  = ',';
        index_row_buf.add_unchecked(escaped_prefix, escaped_prefix_len);
        *index_row_buf.ptr = 0;
        HT_ASSERT(index_row_buf.fill() < index_row_buf.size);
        add_index_row(index_spec, (const char *)index_row_buf.base);
        cell_predicates[cf_spec->get_id()].add_column_predicate(cp, id++);
      }
      else if (cp.operation & (ColumnPredicate::EXACT_MATCH|
                               ColumnPredicate::PREFIX_MATCH)) {

        // every \t in the original value gets escaped
        const char *value;
        size_t value_len;
        lde.escape(cp.value, cp.value_len, &value, &value_len);

        const char *escaped_qualifier;
        size_t escaped_qualifier_len = 0;
        if ((cp.operation & ColumnPredicate::EXACT_MATCH) &&
            (cp.operation & (ColumnPredicate::QUALIFIER_EXACT_MATCH|
                             ColumnPredicate::QUALIFIER_PREFIX_MATCH))) {

          lde.escape(cp.column_qualifier, cp.column_qualifier_len,
                     &escaped_qualifier, &escaped_qualifier_len);
        }

        // exact match:  create row interval ["%d,value\t", ..)
        //          or   create row interval ["%d,value\tqualifier", ..)
        //          or   create row interval ["%d,value\tqualifier\t", ..)
        //          or   create row interval ["%d,value\tqualifier\trow", ..)
        // prefix match: create row interval ["%d,value", ..)
        if (index_row_buf.size < value_len+escaped_qualifier_len+6)
          index_row_buf.grow(value_len+escaped_qualifier_len+6);
        // %d,
        index_row_buf.ptr = (uint8_t*)cfid.append_to((char*)index_row_buf.base);
        *index_row_buf.ptr++  = ',';
        // value
        bool has_row_interval = false;
        index_row_buf.add_unchecked(value, value_len);
        if (cp.operation & ColumnPredicate::EXACT_MATCH) {
          *index_row_buf.ptr++ = '\t';
          // qualifier
          if (cp.operation & (ColumnPredicate::QUALIFIER_EXACT_MATCH|
                              ColumnPredicate::QUALIFIER_PREFIX_MATCH)) {

            index_row_buf.add_unchecked(escaped_qualifier, escaped_qualifier_len);
            if (cp.operation & ColumnPredicate::QUALIFIER_EXACT_MATCH) {
              *index_row_buf.ptr++ = '\t';
              *index_row_buf.ptr = 0;
              // primary row intervals
              if (primary_spec.row_intervals.size()) {
                has_row_interval = true;
                ++row_intervals_applied_count;
                index_row_prefix = (const char *)index_row_buf.base;
                for (const auto &primary_ri : primary_spec.row_intervals) {
                  index_spec.add_row_interval(
                    index_row_prefix + primary_ri.start, primary_ri.start_inclusive,
                    index_row_prefix + primary_ri.end, primary_ri.end_inclusive);
                }
              }
              // primary cell intervals
              else if (primary_spec.cell_intervals.size()) {
                has_row_interval = true;
                index_row_prefix = (const char *)index_row_buf.base;
                for (const auto &primary_ci : primary_spec.cell_intervals) {
                  index_spec.add_row_interval(
                    index_row_prefix + primary_ci.start_row, true,
                    index_row_prefix + primary_ci.end_row, true);
                }
              }
            }
            else
              *index_row_buf.ptr = 0;
          }
          else if (cp.operation & ColumnPredicate::QUALIFIER_REGEX_MATCH) {
            if (Regex::extract_prefix(cp.column_qualifier, cp.column_qualifier_len,
                                  &prefix, &prefix_len, regex_prefix_buf)) {
              const char *escaped_prefix;
              size_t escaped_prefix_len;
              lde.escape(prefix, prefix_len,
                          &escaped_prefix, &escaped_prefix_len);
              index_row_buf.add(escaped_prefix, escaped_prefix_len);
            }
            *index_row_buf.ptr = 0;
          }
          else
            *index_row_buf.ptr = 0;
        }
        if (!has_row_interval) {
          *index_row_buf.ptr = 0;
          HT_ASSERT(index_row_buf.fill() < index_row_buf.size);
          add_index_row(index_spec, (const char *)index_row_buf.base);
        }
        cell_predicates[cf_spec->get_id()].add_column_predicate(cp, id++);
      }
      else if (cp.operation & ColumnPredicate::QUALIFIER_REGEX_MATCH) {
        if (!Regex::extract_prefix(cp.column_qualifier, cp.column_qualifier_len,
                                  &prefix, &prefix_len, regex_prefix_buf))
          return false;
        const char *escaped_prefix;
        size_t escaped_prefix_len;
        lde.escape(prefix, prefix_len,
                    &escaped_prefix, &escaped_prefix_len);
        index_row_prefix.clear();
        index_row_prefix.reserve(5+escaped_prefix_len);
        index_row_prefix = cfid.c_str();
        index_row_prefix.append(",", 1);
        index_row_prefix.append(escaped_prefix, escaped_prefix_len);
        add_index_row(index_spec, index_row_prefix.c_str());
        cell_predicates[cf_spec->get_id()].add_column_predicate(cp, id++);
      }
      else if (cp.operation & (ColumnPredicate::QUALIFIER_EXACT_MATCH|
                               ColumnPredicate::QUALIFIER_PREFIX_MATCH)) {

        const char *escaped_qualifier;
        size_t escaped_qualifier_len;
        lde.escape(cp.column_qualifier, cp.column_qualifier_len,
                   &escaped_qualifier, &escaped_qualifier_len);
        index_row_prefix.clear();
        index_row_prefix.reserve(6+escaped_qualifier_len);
        index_row_prefix = cfid.c_str();
        index_row_prefix.append(",", 1);
        index_row_prefix.append(escaped_qualifier, escaped_qualifier_len);
        if (cp.operation & ColumnPredicate::QUALIFIER_EXACT_MATCH) {
          index_row_prefix.append("\t", 1);
          // primary row intervals
          if (primary_spec.row_intervals.size()) {
            ++row_intervals_applied_count;
            for (const auto &primary_ri : primary_spec.row_intervals) {
              index_spec.add_row_interval(
                index_row_prefix + primary_ri.start, primary_ri.start_inclusive,
                index_row_prefix + primary_ri.end, primary_ri.end_inclusive);
            }
          }
          // primary cell intervals
          else if (primary_spec.cell_intervals.size()) {
            for (const auto &primary_ci : primary_spec.cell_intervals) {
              index_spec.add_row_interval(
                index_row_prefix + primary_ci.start_row, true,
                index_row_prefix + primary_ci.end_row, true);
            }
          }
          else
            add_index_row(index_spec, index_row_prefix.c_str());
        }
        else
          add_index_row(index_spec, index_row_prefix.c_str());
        cell_predicates[cf_spec->get_id()].add_column_predicate(cp, id++);
      }
      else
        return false;
    }

    if (row_intervals_applied)
      *row_intervals_applied = row_intervals_applied_count == primary_spec.column_predicates.size();

    if (qualifier_match_only) {
      if (qualifier_index_count < primary_spec.column_predicates.size())
        return false;
      *use_qualifier = qualifier_match_only;
    }
    else if (value_index_count < primary_spec.column_predicates.size())
      return false;

    return true;
  }

  return false;
}

void TableScannerAsync::transform_primary_scan_spec(ScanSpecBuilder &primary_spec) {

  if (m_use_index)
    return;

  if (!primary_spec.get().column_predicates.empty()) {

    // Load predicate_columns set
    CstrSet predicate_columns;
    for (const auto &predicate : primary_spec.get().column_predicates)
      predicate_columns.insert(predicate.column_family);

    // If selected columns empty, add all columns referenced in predicates
    if (primary_spec.get().columns.empty()) {
      for (auto column : predicate_columns)
        primary_spec.add_column(column);
    }
    else {
      string family;
      const char *colon;
      StringSet selected_columns;
      // If columns selected that are not referenced in predicate, throw error
      for (auto column : primary_spec.get().columns) {
        family.clear();
        if ((colon = strchr(column, ':')) != 0)
          family.append(column, colon-column);
        else
          family.append(column);
        if (predicate_columns.count(family.c_str()) == 0)
          HT_THROWF(Error::HQL_PARSE_ERROR,
                    "Selected column %s must be referenced in Column predicate",
                    column);
        selected_columns.insert(family);
      }

      // Add predicate columns that are missing from selection set
      for (const auto &predicate : primary_spec.get().column_predicates)
        if (selected_columns.count(predicate.column_family) == 0)
          primary_spec.add_column(predicate.column_family);
    }
  }

}

void TableScannerAsync::add_index_row(ScanSpecBuilder &ssb, const char *row)
{
  // this code is identical to the RELOP_SW handler in HqlParser.h
  string tmp;
  RowInterval ri;
  ri.start = row;
  ri.start_inclusive = true;
  const char *str = row;
  const char *end = str + strlen(row);
  const char *ptr;
  for (ptr = end - 1; ptr > str; --ptr) {
    if (::uint8_t(*ptr) < 0xffu) {
      tmp = String(str, ptr - str);
      tmp.append(1, (*ptr)+1);
      ri.end = tmp.c_str();
      ri.end_inclusive = false;
      break;
    }
  }
  if (ptr == str) {
    tmp = row;
    tmp.append(4, (char)0xff);
    ri.end = tmp.c_str();
    ri.end_inclusive = false;
  }
  ssb.add_row_interval(ri.start, ri.start_inclusive,
                    ri.end, ri.end_inclusive);
}

void TableScannerAsync::init(Comm *comm, ApplicationQueueInterfacePtr &app_queue, 
        Table *table, RangeLocatorPtr &range_locator, 
        const ScanSpec &scan_spec, uint32_t timeout_ms, ResultCallback *cb)
{
  int scanner_id = 0;
  IntervalScannerAsyncPtr ri_scanner;
  ScanSpec interval_scan_spec;
  Timer timer(timeout_ms);
  bool current_set = false;

  m_cb->increment_outstanding();
  m_cb->register_scanner(this);

  try {
    if (scan_spec.row_intervals.empty()) {
      if (scan_spec.cell_intervals.empty()) {
        ri_scanner =
          make_shared<IntervalScannerAsync>(comm, app_queue, table, range_locator,
                                            scan_spec, timeout_ms, !current_set,
                                            this, scanner_id++);

        current_set = true;
        m_interval_scanners.push_back(ri_scanner);
        m_outstanding++;
      }
      else {
        m_interval_scanners.reserve(scan_spec.cell_intervals.size());
        for (size_t i=0; i<scan_spec.cell_intervals.size(); i++) {
          scan_spec.base_copy(interval_scan_spec);
          interval_scan_spec.cell_intervals.push_back(
              scan_spec.cell_intervals[i]);
          ri_scanner =
            make_shared<IntervalScannerAsync>(comm, app_queue, table, range_locator,
                                              interval_scan_spec, timeout_ms,
                                              !current_set, this, scanner_id++);
          current_set = true;
          m_interval_scanners.push_back(ri_scanner);
          m_outstanding++;
        }
      }
    }
    else if (scan_spec.scan_and_filter_rows) {
      ScanSpec rowset_scan_spec;
      scan_spec.base_copy(rowset_scan_spec);
      rowset_scan_spec.row_intervals.reserve(scan_spec.row_intervals.size());
      for (const auto &ri : scan_spec.row_intervals) {
        if (ri.start != ri.end && strcmp(ri.start, ri.end) != 0) {
          scan_spec.base_copy(interval_scan_spec);
          interval_scan_spec.scan_and_filter_rows = false;
          interval_scan_spec.row_intervals.push_back(ri);
          ri_scanner =
            make_shared<IntervalScannerAsync>(comm, app_queue, table, range_locator,
                                              interval_scan_spec, timeout_ms,
                                              !current_set, this, scanner_id++);
          current_set = true;
          m_interval_scanners.push_back(ri_scanner);
          m_outstanding++;
        }
        else
          rowset_scan_spec.row_intervals.push_back(ri);
      }
      if (rowset_scan_spec.row_intervals.size()) {
        ri_scanner =
          make_shared<IntervalScannerAsync>(comm, app_queue, table, range_locator,
                                            rowset_scan_spec, timeout_ms, 
                                            !current_set, this, scanner_id++);
        current_set = true;
        m_interval_scanners.push_back(ri_scanner);
        m_outstanding++;
      }
    }
    else {
      m_interval_scanners.reserve(scan_spec.row_intervals.size());
      for (size_t i=0; i<scan_spec.row_intervals.size(); i++) {
        scan_spec.base_copy(interval_scan_spec);
        interval_scan_spec.row_intervals.push_back(scan_spec.row_intervals[i]);
        ri_scanner =
          make_shared<IntervalScannerAsync>(comm, app_queue, table, range_locator,
                                            interval_scan_spec, timeout_ms, 
                                            !current_set, this, scanner_id++);
        current_set = true;
        m_interval_scanners.push_back(ri_scanner);
        m_outstanding++;
      }
    }
  }
  catch (Exception &e) {
    m_error = e.code();
    m_error_msg = e.what();
    if (ri_scanner && ri_scanner->has_outstanding_requests()) {
      m_interval_scanners.push_back(ri_scanner);
      m_outstanding++;
    }
    if (m_outstanding == 0)
      maybe_callback_error(0, false);
  }
}

TableScannerAsync::~TableScannerAsync() {
  try {
    cancel();
    wait_for_completion();
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
  }
  if (m_use_index) {
    ((IndexScannerCallback *)m_cb)->shutdown();
    delete m_cb;
    m_cb = 0;
  }
}

void TableScannerAsync::cancel() {
  unique_lock<mutex> lock(m_cancel_mutex);
  m_cancelled = true;
}

bool TableScannerAsync::is_cancelled() {
  unique_lock<mutex> lock(m_cancel_mutex);
  return m_cancelled;
}

void TableScannerAsync::handle_error(int scanner_id, int error, const string &error_msg,
                                     bool is_create) {
  bool cancelled = is_cancelled();
  unique_lock<mutex> lock(m_mutex);
  bool abort = false;
  bool next = false;

  // if we've already seen an error or the scanner has been cacncelled
  if (m_error != Error::OK || cancelled) {
    abort=true;
    next = m_interval_scanners[scanner_id]->abort(is_create);
  }
  else {
    switch(error) {
      case (Error::TABLE_NOT_FOUND):
        if (m_table->auto_refresh() && is_create)
        abort = !(m_interval_scanners[scanner_id]->retry_or_abort(true, true, 
                    is_create, &next, error));
        else {
          next = m_interval_scanners[scanner_id]->abort(is_create);
          abort = true;
        }
        break;
      case (Error::RANGESERVER_GENERATION_MISMATCH):
        if (m_table->auto_refresh() && is_create)
        abort = !(m_interval_scanners[scanner_id]->retry_or_abort(true, false, 
                    is_create, &next, error));
        else {
          next = m_interval_scanners[scanner_id]->abort(is_create);
          abort = true;
        }
        break;
      case(Error::RANGESERVER_INVALID_SCANNER_ID):
        abort = !(m_interval_scanners[scanner_id]->is_destroyed_scanner(is_create));
        next = !m_interval_scanners[scanner_id]->has_outstanding_requests();
        break;
      case(Error::RANGESERVER_RANGE_NOT_FOUND):
      case(Error::COMM_NOT_CONNECTED):
      case(Error::COMM_BROKEN_CONNECTION):
        abort = !(m_interval_scanners[scanner_id]->retry_or_abort(false, true, 
                    is_create, &next, error));
        break;

      default:
        Exception e(error, error_msg);
        HT_ERROR_OUT <<  "Received error: is_create=" << is_create << " - "<< e << HT_END;
        next = m_interval_scanners[scanner_id]->abort(is_create);
        abort = true;
    }
  }

  // if we've seen an error before then don't bother with callback
  if (m_error != Error::OK || cancelled) {
    maybe_callback_error(scanner_id, next);
    if (next && scanner_id == m_current_scanner)
      move_to_next_interval_scanner(scanner_id);
    return;
  }
  else if (abort) {
    Exception e(error, error_msg);
    m_error = error;
    m_error_msg = error_msg;
    HT_ERROR_OUT << e << HT_END;
    maybe_callback_error(scanner_id, next);
    if (next && scanner_id == m_current_scanner)
      move_to_next_interval_scanner(scanner_id);
  }
  else if (next && scanner_id == m_current_scanner) {
    move_to_next_interval_scanner(scanner_id);
  }
}

void TableScannerAsync::handle_timeout(int scanner_id, const string &error_msg, bool is_create) {
  bool cancelled = is_cancelled();
  unique_lock<mutex> lock(m_mutex);
  bool next;

  next = m_interval_scanners[scanner_id]->abort(is_create);
  // if we've seen an error before or scanner has been cancelled then don't bother with callback
  if (m_error != Error::OK || cancelled) {
   maybe_callback_error(scanner_id, next);
   return;
  }

  HT_ERROR_OUT << "Unable to complete scan request within " << m_timeout_ms
               << " - " << error_msg << HT_END;
  m_error = Error::REQUEST_TIMEOUT;
  maybe_callback_error(scanner_id, next);
  if (next && scanner_id == m_current_scanner)
    move_to_next_interval_scanner(scanner_id);

}

void TableScannerAsync::handle_result(int scanner_id, EventPtr &event, bool is_create) {

  bool cancelled = is_cancelled();
  unique_lock<mutex> lock(m_mutex);
  ScanCellsPtr cells;

  // abort interval scanners if we've seen an error previously or scanned has been cancelled
  bool abort = (m_error != Error::OK || cancelled);

  bool next;
  bool do_callback = false;
  int current_scanner = scanner_id;

  try {
    // If the scan already encountered an error/cancelled
    // don't bother calling into callback anymore
    if (abort) {
      next = m_interval_scanners[scanner_id]->abort(is_create);
      if (cancelled && m_error == Error::OK) {
        // scanner was cancelled and is over
        if (next && m_outstanding==1) {
          do_callback = true;
          cells = make_shared<ScanCells>();
        }
        else
          do_callback = false;
        maybe_callback_ok(scanner_id, next, do_callback, cells);
      }
      else
        maybe_callback_error(scanner_id, next);
    }
    else {
      // send results to interval scanner
      next = m_interval_scanners[scanner_id]->handle_result(&do_callback, cells, event, is_create);
      maybe_callback_ok(scanner_id, next, do_callback, cells);
    }

    if (next)
      move_to_next_interval_scanner(current_scanner);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    m_error = e.code();
    m_error_msg = e.what();
    next = !m_interval_scanners[current_scanner]->has_outstanding_requests();
    maybe_callback_error(current_scanner, next);
    throw;
  }
}

void TableScannerAsync::maybe_callback_error(int scanner_id, bool next) {
  bool eos = false;
  // ok to update m_outstanding since caller has locked mutex
  if (next) {
    HT_ASSERT(m_outstanding>0 && m_interval_scanners[scanner_id] != 0);
    m_outstanding--;
    // Aggregate profile data
    m_profile_data += m_interval_scanners[scanner_id]->profile_data();
    m_interval_scanners[scanner_id] = 0;
  }

  if (m_outstanding == 0) {
    m_stopwatch.stop();
    m_profile_data.elapsed_time_millis = m_stopwatch.elapsed_millis();
    eos = true;
  }

  m_cb->scan_error(this, m_error, m_error_msg, eos);

  if (eos) {
    m_cb->deregister_scanner(this);
    m_cb->decrement_outstanding();
    m_cond.notify_all();
  }
}

void TableScannerAsync::maybe_callback_ok(int scanner_id, bool next, bool do_callback, ScanCellsPtr &cells) {
  bool eos = false;
  // ok to update m_outstanding since caller has locked mutex
  if (next) {
    HT_ASSERT(m_outstanding>0 && m_interval_scanners[scanner_id] != 0);
    m_outstanding--;
    // Aggregate profile data
    m_profile_data += m_interval_scanners[scanner_id]->profile_data();
    m_interval_scanners[scanner_id] = 0;
  }

  if (m_outstanding == 0) {
    m_stopwatch.stop();
    m_profile_data.elapsed_time_millis = m_stopwatch.elapsed_millis();
    eos = true;
  }

  if (do_callback) {
    if (eos)
      cells->set_eos();
    HT_ASSERT(cells != 0);
    m_cb->scan_ok(this, cells);
  }

  if (m_outstanding==0) {
    m_cb->deregister_scanner(this);
    m_cb->decrement_outstanding();
    m_cond.notify_all();
  }
}

void TableScannerAsync::wait_for_completion() {
  unique_lock<mutex> lock(m_mutex);
  m_cond.wait(lock, [this](){ return m_outstanding == 0; });
}

String TableScannerAsync::get_table_name() const {
  return m_table->get_name();
}

void TableScannerAsync::move_to_next_interval_scanner(int current_scanner) {
  bool next = true;
  bool cancelled = is_cancelled();
  bool do_callback;
  ScanCellsPtr cells;
  bool abort = cancelled || (m_error != Error::OK);

  while (next && m_outstanding && current_scanner < ((int)m_interval_scanners.size())-1) {
    current_scanner++;
    // unless the scan has been aborted we should be going through scanners in order
    if (current_scanner != m_current_scanner+1) {
      HT_ASSERT(abort);
      break;
    }
    m_current_scanner = current_scanner;

    if (m_interval_scanners[current_scanner] !=0) {
      next = m_interval_scanners[current_scanner]->set_current(&do_callback, cells, abort);
      HT_ASSERT(do_callback || !next || abort);

      // this is the last outstanding scanner and the scan was cancelled
      // or failed with an error
      if (next 
          && m_outstanding==1 
          && ((cancelled && m_error == Error::OK)
            || m_error != Error::OK)) {
        do_callback = true;
        cells = make_shared<ScanCells>();
      }
      maybe_callback_ok(m_current_scanner, next, do_callback, cells);
    }
  }

  // if we skipped ALL outstanding scanners then make sure the "eos" marker
  // is sent to the caller, and m_outstanding is decremented
  if (next 
      && m_outstanding == 1
      && current_scanner == ((int)m_interval_scanners.size() - 1) 
      && !cells) {
    cells = make_shared<ScanCells>();
    maybe_callback_ok(m_current_scanner, true, true, cells);
    m_current_scanner = (int)m_interval_scanners.size() - 1;
  }
}
