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

#include "IntervalScannerAsync.h"

#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/Table.h>

#include <Common/Error.h>
#include <Common/String.h>

#include <cassert>
#include <chrono>
#include <vector>

using namespace Hypertable;
using namespace Hypertable::Lib;
using namespace std;

namespace {
  enum {
    ABORTED = 1,
    RESTART = 2
  };
}

IntervalScannerAsync::IntervalScannerAsync(Comm *comm, ApplicationQueueInterfacePtr &app_queue,
    Table *table, RangeLocatorPtr &range_locator, const ScanSpec &scan_spec,
    uint32_t timeout_ms, bool current, TableScannerAsync *scanner, int id)
  : m_table(table), m_range_locator(range_locator),
    m_loc_cache(range_locator->location_cache()),
    m_scan_limit_state(scan_spec), m_range_server(comm, timeout_ms), m_eos(false),
    m_fetch_outstanding(false), m_create_outstanding(false),
    m_end_inclusive(false), m_timeout_ms(timeout_ms),
    m_current(current), m_bytes_scanned(0),
    m_create_handler(app_queue, scanner, id, true),
    m_fetch_handler(app_queue, scanner, id, false),
    m_create_timer(timeout_ms), m_fetch_timer(timeout_ms),
    m_cur_scanner_finished(false), m_cur_scanner_id(0), m_state(0),
    m_create_event_saved(false), m_invalid_scanner_id_ok(false) {

  HT_ASSERT(m_timeout_ms);

  table->get(m_table_identifier, m_schema);
  init(scan_spec);
}


void IntervalScannerAsync::init(const ScanSpec &scan_spec) {
  const char *start_row, *end_row;
  bool start_row_inclusive=true;

  if (!scan_spec.row_intervals.empty() && !scan_spec.cell_intervals.empty())
    HT_THROW(Error::BAD_SCAN_SPEC,
             "ROW predicates and CELL predicates can't be combined");

  m_range_server.set_default_timeout(m_timeout_ms);
  m_rowset.clear();

  m_scan_spec_builder.clear();
  m_scan_spec_builder.set_row_limit(scan_spec.row_limit);
  m_scan_spec_builder.set_cell_limit(scan_spec.cell_limit);
  m_scan_spec_builder.set_cell_limit_per_family(scan_spec.cell_limit_per_family);
  m_scan_spec_builder.set_max_versions(scan_spec.max_versions);
  m_scan_spec_builder.set_row_regexp(scan_spec.row_regexp);
  m_scan_spec_builder.set_value_regexp(scan_spec.value_regexp);
  m_scan_spec_builder.set_row_offset(scan_spec.row_offset);
  m_scan_spec_builder.set_cell_offset(scan_spec.cell_offset);
  m_scan_spec_builder.set_do_not_cache(scan_spec.do_not_cache);
  m_scan_spec_builder.set_rebuild_indices(scan_spec.rebuild_indices);
  m_scan_spec_builder.set_debug(scan_spec.debug);

  for (const auto &cp : scan_spec.column_predicates)
    m_scan_spec_builder.add_column_predicate(cp.column_family,
            cp.column_qualifier, cp.operation, cp.value, cp.value_len);

  string family;
  const char *colon;
  for (size_t i=0; i<scan_spec.columns.size(); i++) {
    colon = strchr(scan_spec.columns[i], ':');
    family = colon ? String(scan_spec.columns[i], colon-scan_spec.columns[i]) :
      String(scan_spec.columns[i]);
    if (m_schema->get_column_family(family.c_str()) == 0)
      HT_THROW(Error::RANGESERVER_INVALID_COLUMNFAMILY,
      (String)"Table= " + m_table->get_name() + " , Column family=" + scan_spec.columns[i]);
    m_scan_spec_builder.add_column(scan_spec.columns[i]);
  }

  HT_ASSERT(scan_spec.row_intervals.size() <= 1 || scan_spec.scan_and_filter_rows);
  if (!scan_spec.row_intervals.empty()) {
    if (!scan_spec.scan_and_filter_rows) {
      start_row = (scan_spec.row_intervals[0].start == 0) ? ""
          : scan_spec.row_intervals[0].start;
      start_row_inclusive = scan_spec.row_intervals[0].start_inclusive;
      if (scan_spec.row_intervals[0].end == 0 ||
          scan_spec.row_intervals[0].end[0] == 0)
        end_row = Key::END_ROW_MARKER;
      else
        end_row = scan_spec.row_intervals[0].end;
      int cmpval = strcmp(start_row, end_row);
      if (cmpval > 0)
        HT_THROW(Error::BAD_SCAN_SPEC, format("start_row (%s) > end_row (%s)", start_row, end_row));
      if (cmpval == 0 && !scan_spec.row_intervals[0].start_inclusive
          && !scan_spec.row_intervals[0].end_inclusive)
        HT_THROW(Error::BAD_SCAN_SPEC, "empty row interval");
      m_start_row = start_row;

      m_end_row = end_row;
      m_end_inclusive = scan_spec.row_intervals[0].end_inclusive;
      m_scan_spec_builder.add_row_interval(start_row,
          scan_spec.row_intervals[0].start_inclusive, end_row,
          scan_spec.row_intervals[0].end_inclusive);
    }
    else {
      m_scan_spec_builder.set_scan_and_filter_rows(true);
      // order and filter duplicated rows
      CstrSet rowset;
      for (const auto &ri : scan_spec.row_intervals)
        rowset.insert(ri.start); // ri.start always equals to ri.end
      // setup ordered row intervals and rowset
      m_scan_spec_builder.reserve_rows(rowset.size());
      for (auto r : rowset) {
        // end is set to "" in order to safe space
        m_scan_spec_builder.add_row_interval(r, true, "", true);
        // Cstr's must be taken from m_scan_spec_builder and not from scan_spec
        m_rowset.insert(m_scan_spec_builder.get().row_intervals.back().start);
      }
      m_start_row = *m_rowset.begin();

      m_end_row = *m_rowset.rbegin();
      m_end_inclusive = true;
    }
  }
  else if (!scan_spec.cell_intervals.empty()) {
    start_row = (scan_spec.cell_intervals[0].start_row == 0) ? ""
        : scan_spec.cell_intervals[0].start_row;

    if (scan_spec.cell_intervals[0].start_column == 0)
      HT_THROW(Error::BAD_SCAN_SPEC,
               "Bad cell interval (start_column == NULL)");
    if (scan_spec.cell_intervals[0].end_row == 0 ||
        scan_spec.cell_intervals[0].end_row[0] == 0)
      end_row = Key::END_ROW_MARKER;
    else
      end_row = scan_spec.cell_intervals[0].end_row;
    if (scan_spec.cell_intervals[0].end_column == 0)
      HT_THROW(Error::BAD_SCAN_SPEC,
               "Bad cell interval (end_column == NULL)");
    int cmpval = strcmp(start_row, end_row);
    if (cmpval > 0)
      HT_THROW(Error::BAD_SCAN_SPEC, "start_row > end_row");
    if (cmpval == 0) {
      int cmpval = strcmp(scan_spec.cell_intervals[0].start_column,
                          scan_spec.cell_intervals[0].end_column);
      if (cmpval == 0 && !scan_spec.cell_intervals[0].start_inclusive
          && !scan_spec.cell_intervals[0].end_inclusive)
        HT_THROW(Error::BAD_SCAN_SPEC, "empty cell interval");
    }
    m_scan_spec_builder.add_cell_interval(start_row,
        scan_spec.cell_intervals[0].start_column,
        scan_spec.cell_intervals[0].start_inclusive,
        end_row, scan_spec.cell_intervals[0].end_column,
        scan_spec.cell_intervals[0].end_inclusive);

    m_start_row = start_row;
    m_end_row = end_row;
    m_end_inclusive = true;
  }
  else {
    m_start_row = "";
    m_end_row = Key::END_ROW_MARKER;
    m_scan_spec_builder.add_row_interval("", false, Key::END_ROW_MARKER, false);
  }
  m_scan_spec_builder.set_time_interval(scan_spec.time_interval.first,
                                        scan_spec.time_interval.second);

  m_scan_spec_builder.set_return_deletes(scan_spec.return_deletes);
  m_scan_spec_builder.set_keys_only(scan_spec.keys_only);

  // If offset or limit specified, defer readahead until outstanding result is
  // processed
  m_defer_readahead =
    m_scan_spec_builder.get().cell_offset ||
    m_scan_spec_builder.get().cell_limit ||
    m_scan_spec_builder.get().row_offset ||
    m_scan_spec_builder.get().row_limit;

  // start scan asynchronously (can trigger table not found exceptions)
  m_create_scanner_row = m_start_row;
  if (!start_row_inclusive)
    m_create_scanner_row.append(1,1);
  find_range_and_start_scan(m_create_scanner_row.c_str());
  HT_ASSERT(m_create_outstanding && !m_fetch_outstanding);
}

IntervalScannerAsync::~IntervalScannerAsync() {
  try {
    // destroy dangling scanner
    if (m_cur_scanner_id && !m_cur_scanner_finished)
      m_range_server.destroy_scanner(m_range_info.addr, m_cur_scanner_id, 0);
  }
  catch(Exception &e) {
    HT_ERROR_OUT << e << HT_END;
  }
}

// caller is responsible for state of m_create_timer
void IntervalScannerAsync::find_range_and_start_scan(const char *row_key, bool hard) {

  RangeSpec  range;
  DynamicBuffer dbuf(0);

  m_create_timer.start();

  // if rowset scan adjust row intervals
  if (!m_rowset.empty()) {
    RowIntervals& row_intervals = m_scan_spec_builder.get().row_intervals;
    while (row_intervals.size() && strcmp(row_intervals.front().start, row_key) < 0)
      row_intervals.erase(row_intervals.begin());
  }

 try_again:

  try {
    m_range_locator->find_loop(&m_table_identifier, row_key,
                               &m_next_range_info, m_create_timer, hard);
  }
  catch (Exception &e) {
    if (e.code() == Error::REQUEST_TIMEOUT)
      HT_THROW2(e.code(), e, e.what());
    this_thread::sleep_for(chrono::milliseconds(1000));
    if (m_create_timer.expired())
      HT_THROW(Error::REQUEST_TIMEOUT, e.what());
    goto try_again;
  }

  while (true) {
    set_range_spec(dbuf, range);
    try {
      HT_ASSERT(!m_create_outstanding && !m_create_event_saved && !m_eos);
      // create scanner asynchronously
      m_create_outstanding = true;
      m_range_server.create_scanner(m_next_range_info.addr, m_table_identifier, range,
                                    m_scan_spec_builder.get(), &m_create_handler, m_create_timer);
      HT_ASSERT(m_next_range_info.addr.is_proxy());
      m_profile_data.servers.insert(m_next_range_info.addr.proxy);
                
    }
    catch (Exception &e) {
      string msg = format("Problem creating scanner at %s on %s[%s..%s] - %s",
                          m_next_range_info.addr.to_str().c_str(), m_table_identifier.id,
                          range.start_row, range.end_row, e.what());
      reset_outstanding_status(true, false);
      if (e.code() != Error::REQUEST_TIMEOUT &&
          e.code() != Error::COMM_NOT_CONNECTED &&
          e.code() != Error::COMM_BROKEN_CONNECTION &&
          e.code() != Error::COMM_INVALID_PROXY) {
        HT_ERROR_OUT << e << HT_END;
        HT_THROW2(e.code(), e, msg);
      }
      else if (m_create_timer.remaining() <= 1000) {
        uint32_t duration  = m_create_timer.duration();
        HT_ERRORF("Scanner creation request will time out. Initial timer "
                  "duration %d (last error = %s - %s)", (int)duration,
                  Error::get_text(e.code()), e.what());
        HT_THROW2(Error::REQUEST_TIMEOUT, e, msg + format(". Unable to "
                  "complete request within %d ms", (int)duration));
      }

      this_thread::sleep_for(chrono::milliseconds(1000));

      // try again, the hard way
      m_range_locator->find_loop(&m_table_identifier, row_key,
                                 &m_next_range_info, m_create_timer, true);
      continue;
    }
    break;
  }
}

void IntervalScannerAsync::reset_outstanding_status(bool is_create, bool reset_timer) {
  if (is_create) {
    HT_ASSERT(m_create_outstanding && !m_create_event_saved);
    m_create_outstanding = false;
    if (reset_timer)
      m_create_timer.reset();
  }
  else {
    HT_ASSERT(m_fetch_outstanding && (m_current || m_invalid_scanner_id_ok));
    m_fetch_outstanding = false;
    if (reset_timer)
      m_fetch_timer.reset();
  }
}

bool IntervalScannerAsync::abort(bool is_create) {
 reset_outstanding_status(is_create, true);
  m_eos = true;
  m_state = ABORTED;
  bool move_to_next = m_current && !has_outstanding_requests();
  if (move_to_next)
    m_current = false;
  return move_to_next;
}

bool IntervalScannerAsync::is_destroyed_scanner(bool is_create) {
  // handle case where row limit was hit and scanner was cancelled but fetch request is
  // still outstanding
  reset_outstanding_status(is_create, true);
  if (m_invalid_scanner_id_ok) {
    HT_ASSERT(!is_create);
    HT_ASSERT(!has_outstanding_requests());
    m_invalid_scanner_id_ok = false;
    return true;
  }
  return false;
}

bool IntervalScannerAsync::retry_or_abort(bool refresh, bool hard, bool is_create,
                                          bool *move_to_next, int last_error) {
  uint32_t wait_time = 1000;
  reset_outstanding_status(is_create, false);

  if (m_eos) {
    *move_to_next = !has_outstanding_requests() && m_current;
    if (*move_to_next)
      m_current = false;
    return true;
  }

  // RangeServer has already destroyed scanner so we can't refresh
  if (!is_create && refresh) {
    HT_ERROR_OUT << "Table schema can't be refreshed when schema changes after scanner creation"
                 << HT_END;
    // retry failed, abort scanner
    *move_to_next = !has_outstanding_requests() && m_current;
    if (*move_to_next)
      m_current = false;
    m_eos = true;
    m_state = ABORTED;
    return false;
  }

  // no point refreshing, since the wait will cause a timeout
  if (is_create && refresh && m_create_timer.remaining() < wait_time) {
    uint32_t duration  = m_create_timer.duration();
    HT_ERRORF("Scanner creation request will time out. Initial timer "
              "duration %d", (int)duration);
    // retry failed, abort scanner
    *move_to_next = !has_outstanding_requests() && m_current;
    if (*move_to_next)
      m_current = false;
    m_eos = true;
    m_state = ABORTED;
    return false;
  }

  if (last_error == Error::COMM_NOT_CONNECTED ||
      last_error == Error::COMM_BROKEN_CONNECTION ||
      last_error == Error::COMM_INVALID_PROXY)
    m_state = RESTART;

  if (m_state == RESTART) {
    if (!has_outstanding_requests())
      restart_scan(refresh);
    *move_to_next = (m_state==ABORTED) &&
      !has_outstanding_requests() && m_current;
    return m_state != ABORTED;
  }

  try {
    if (refresh)
      m_table->refresh(m_table_identifier, m_schema);
    // wait a bit before kicking off the scan again
    // this_thread::sleep_for(chrono::milliseconds(wait_time));
    find_range_and_start_scan(m_create_scanner_row.c_str(), hard);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    if (m_create_outstanding) {
      reset_outstanding_status(is_create, false);
    }
    // retry failed, abort scanner
    *move_to_next = !has_outstanding_requests() && m_current;
    if (*move_to_next)
      m_current = false;
    m_eos = true;
    m_state = ABORTED;
    return false;
  }

  HT_ASSERT (!m_current ||  m_eos || has_outstanding_requests());
  *move_to_next = !has_outstanding_requests() && m_current;
  if (*move_to_next)
    m_current = false;
  return true;
}

void IntervalScannerAsync::set_range_spec(DynamicBuffer &dbuf, RangeSpec &range) {
  dbuf.ensure(m_next_range_info.start_row.length()
              + m_next_range_info.end_row.length() + 2);
  range.start_row = (char *)dbuf.add_unchecked(m_next_range_info.start_row.c_str(),
      m_next_range_info.start_row.length()+1);
  range.end_row   = (char *)dbuf.add_unchecked(m_next_range_info.end_row.c_str(),
      m_next_range_info.end_row.length()+1);
}

bool IntervalScannerAsync::handle_result(bool *show_results, ScanCellsPtr &cells,
    EventPtr &event, bool is_create) {

  reset_outstanding_status(is_create, true);

  // deal with outstanding fetch/create for aborted scanner
  if (m_eos) {
    if (m_state == ABORTED)
      // scan was aborted caller shd have shown error on first occurrence
      *show_results = false;
    else {
      // scan is over but there was a create/fetch outstanding, send a ScanCells with 0 cells
      // and just the eos bit set
      *show_results = true;
      cells = make_shared<ScanCells>();
    }
    return !has_outstanding_requests();
  }

  *show_results = m_current;
  // if this event is from a fetch scanblock
  if (!is_create) {
    set_result(event, cells, is_create);
    load_result(cells);
    if (!has_outstanding_requests()) {
      if (m_state == RESTART)
        restart_scan();
      else if (m_eos)
        m_current = false;
    }
  }
  // else this event is from a create scanner result
  else {
    // if this scanner is current
    if (m_current) {
      // if there is a fetch that is outstanding
      if (m_fetch_outstanding) {
        *show_results = false;
        // save this event for now
        m_create_event = event;
        m_create_event_saved = true;
      }
      // got results from create_scanner and theres no outstanding fetch request
      else {
        // set the range_info and load cells
        if (m_state != RESTART) {
          m_range_info = m_next_range_info;
          set_result(event, cells, is_create);
          load_result(cells);
        }
        else {
          // Send back an empty ScanCells object
          cells = make_shared<ScanCells>();
        }
        if (!has_outstanding_requests()) {
          if (m_state == RESTART)
            restart_scan();
          else if (m_eos)
            m_current = false;
        }
      }
    }
    else {
      // this scanner is not current, just enqueue for now
      HT_ASSERT(!m_create_event_saved);
      m_create_event = event;
      m_create_event_saved = true;
    }
  }
  HT_ASSERT (!m_current ||  m_eos || has_outstanding_requests());
  return (m_eos && !has_outstanding_requests());
}

void IntervalScannerAsync::set_result(EventPtr &event, ScanCellsPtr &cells,
                                      bool is_create) {
  cells = make_shared<ScanCells>();
  m_cur_scanner_finished = cells->add(event, &m_cur_scanner_id);

  if (is_create)
    m_profile_data.subscanners++;

  // if there was an OFFSET (or CELL_OFFSET) predicate in the query and the
  // RangeServer actually skipped rows (or cells) because of this predicate
  // then adjust the ScanSpec for the next scanner.
  int skipped_rows = 0;
  int skipped_cells = 0;
  if (is_create) {
    skipped_rows = cells->get_skipped_rows();
    skipped_cells = cells->get_skipped_cells();
    if (skipped_rows) {
      HT_ASSERT(skipped_cells == 0);
      HT_ASSERT(m_scan_spec_builder.get().row_offset >= skipped_rows);
      m_scan_spec_builder.get().row_offset -= skipped_rows;
    }
    if (skipped_cells) {
      HT_ASSERT(skipped_rows == 0);
      HT_ASSERT(m_scan_spec_builder.get().cell_offset >= skipped_cells);
      m_scan_spec_builder.get().cell_offset -= skipped_cells;
    }
  }

  // current scanner is finished but we have results saved from the next scanner
  if (m_cur_scanner_finished && m_create_event_saved) {
    HT_ASSERT(skipped_rows == 0 && skipped_cells == 0);
    HT_ASSERT(!has_outstanding_requests());
    m_create_event_saved = false;
    m_range_info = m_next_range_info;
    m_cur_scanner_finished = cells->add(m_create_event, &m_cur_scanner_id);
    m_profile_data.subscanners++;
  }

  m_profile_data += cells->profile_data();
}

void IntervalScannerAsync::load_result(ScanCellsPtr &cells) {
  Key last_key;

  // if scan is not over, current scanner is finished and next create scanner results
  // arrived then add them to cells
  bool eos;

  if (m_state != RESTART && !m_defer_readahead)
    readahead();

  if (m_last_key.row)
    last_key = m_last_key;

  eos = cells->load(m_schema, m_end_row, m_end_inclusive, &m_scan_limit_state,
		    m_rowset, &m_bytes_scanned, &last_key);

  m_eos = m_eos || eos;

  // if scan is over but current scanner is not finished then destroy it
  if (m_eos) {
    if (!m_cur_scanner_finished) {
      try {
        m_range_server.destroy_scanner(m_range_info.addr, m_cur_scanner_id, 0);
      }
      catch (Exception &e) {
        HT_ERROR_OUT << e << HT_END;
      }
      m_invalid_scanner_id_ok=true;
      m_cur_scanner_id = 0;
    }
  }
  else {
    // Record the last key seen in case we need to restart
    if (last_key.row && last_key.serial.ptr != m_last_key.serial.ptr) {
      m_last_key_buf.clear();
      m_last_key_buf.add(last_key.serial.ptr, last_key.length);
      m_last_key.load( SerializedKey(m_last_key_buf.base) );
    }
  }

  if (!m_eos && m_state != RESTART && m_defer_readahead)
    readahead();

  return;
}

bool IntervalScannerAsync::set_current(bool *show_results, ScanCellsPtr &cells, bool abort) {

  HT_ASSERT(!m_fetch_outstanding && !m_current);
  m_current = true;
  *show_results = false;
  if (m_create_outstanding)
    return false;

  if (abort) {
    m_eos = true;
  }
  if (m_eos) {
    m_current = false;
    *show_results = false;
    HT_ASSERT(!has_outstanding_requests());
    return true;
  }
  *show_results = true;
  HT_ASSERT(!has_outstanding_requests() && m_create_event_saved);
  m_create_event_saved = false;
  m_range_info = m_next_range_info;
  set_result(m_create_event, cells);
  HT_ASSERT(m_state != RESTART);
  load_result(cells);
  if (!has_outstanding_requests()) {
    if (m_state == RESTART)
      restart_scan();
    else if (m_eos)
      m_current = false;
  }
  HT_ASSERT (!m_current ||  m_eos || has_outstanding_requests());
  return m_eos && !(has_outstanding_requests());
}

void IntervalScannerAsync::readahead() {
  if (m_eos)
    return;

  // if the current scanner is not finished
  if (!m_cur_scanner_finished) {
    HT_ASSERT(!m_fetch_outstanding && !m_eos && m_current);
    // request next scanblock and block
    try {
      m_fetch_timer.start();
      m_fetch_outstanding = true;
      m_range_server.fetch_scanblock(m_range_info.addr, m_cur_scanner_id,
                                     &m_fetch_handler, m_fetch_timer);
    }
    catch (Exception &e) {
      m_fetch_outstanding = false;
      m_fetch_timer.reset();
      if (e.code() == Error::COMM_NOT_CONNECTED ||
          e.code() == Error::COMM_BROKEN_CONNECTION ||
          e.code() == Error::COMM_INVALID_PROXY) {
        HT_ASSERT(m_state == 0);
        m_state = RESTART;
        return;
      }
      HT_THROW2F(e.code(), e, "Problem calling RangeServer::fetch_scanblock(%s, sid=%d)",
                 m_range_info.addr.proxy.c_str(), (int)m_cur_scanner_id);
    }

    if (m_defer_readahead)
      return;
  }
  // if the current scanner is finished
  else {
    // if this range ends with END_ROW_MARKER OR the end row of scan is beyond the range end
    if (!strcmp(m_range_info.end_row.c_str(), Key::END_ROW_MARKER) ||
        m_end_row.compare(m_range_info.end_row) <= 0) {
      // this interval scanner is finished
      m_eos = true;
    }
  }
  // if there is no create outstanding or saved, scan is not over,
  // and the range being scanned by the current scanner is not the last range in the scan
  // then create a scanner for the next range
  if (!m_create_outstanding && !m_create_event_saved && !m_eos &&
       m_end_row.compare(m_range_info.end_row) > 0 &&
       strcmp(m_range_info.end_row.c_str(), Key::END_ROW_MARKER)) {
    m_create_scanner_row = m_range_info.end_row;
    // if rowset scan update start row for next range
    if (!m_rowset.empty())
      if (m_create_scanner_row.compare(*m_rowset.begin()) < 0)
        m_create_scanner_row = *m_rowset.begin();

    m_create_scanner_row.append(1,1);  // construct row key in next range

    find_range_and_start_scan(m_create_scanner_row.c_str());
  }
  return;
}


void IntervalScannerAsync::restart_scan(bool refresh) {

  HT_ASSERT(m_state == RESTART);

  try {
    if (refresh)
      m_table->refresh(m_table_identifier, m_schema);

    m_create_event_saved = false;
    m_create_event = 0;

    if (m_last_key.row)
      m_create_scanner_row = m_last_key.row;

    m_state = 0;
    find_range_and_start_scan(m_create_scanner_row.c_str(), true);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    m_eos = true;
    m_state = ABORTED;
  }
}
