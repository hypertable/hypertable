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

#ifndef Hypertable_Lib_ScanSpec_h
#define Hypertable_Lib_ScanSpec_h

#include <Hypertable/Lib/CellInterval.h>
#include <Hypertable/Lib/ColumnPredicate.h>
#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/RowInterval.h>
#include <Hypertable/Lib/TableParts.h>

#include <Common/PageArenaAllocator.h>
#include <Common/Serializable.h>

#include <boost/noncopyable.hpp>

#include <cstring>
#include <vector>

namespace Hypertable {
namespace Lib {

  using namespace std;

  typedef PageArenaAllocator<RowInterval> RowIntervalAlloc;
  typedef vector<RowInterval, RowIntervalAlloc> RowIntervals;

  typedef PageArenaAllocator<CellInterval> CellIntervalAlloc;
  typedef vector<CellInterval, CellIntervalAlloc> CellIntervals;

  typedef PageArenaAllocator<const char *> CstrAlloc;
  typedef vector<const char *, CstrAlloc> CstrColumns;

  typedef PageArenaAllocator<ColumnPredicate> ColumnPredicateAlloc;
  typedef vector<ColumnPredicate, ColumnPredicateAlloc> ColumnPredicates;

  /// Scan predicate and control specification.
  class ScanSpec : public Serializable {
  public:
    ScanSpec() : time_interval(TIMESTAMP_MIN, TIMESTAMP_MAX) { }
    ScanSpec(CharArena &arena)
      : columns(CstrAlloc(arena)),
        row_intervals(RowIntervalAlloc(arena)),
        cell_intervals(CellIntervalAlloc(arena)),
        column_predicates(ColumnPredicateAlloc(arena)),
        time_interval(TIMESTAMP_MIN, TIMESTAMP_MAX) { }
    ScanSpec(CharArena &arena, const ScanSpec &);
    ScanSpec(const uint8_t **bufp, size_t *remainp) { decode(bufp, remainp); }

    /// Renders scan spec as an HQL SELECT statement.
    /// @param table Name of table being selected
    /// @return HQL SELECT statement.
    const string render_hql(const string &table) const;

    void clear() {
      row_limit = 0;
      cell_limit = 0;
      cell_limit_per_family = 0;
      row_offset = 0;
      cell_offset = 0;
      max_versions = 0;
      columns.clear();
      row_intervals.clear();
      cell_intervals.clear();
      column_predicates.clear();
      time_interval.first = TIMESTAMP_MIN;
      time_interval.second = TIMESTAMP_MAX;
      rebuild_indices.clear();
      keys_only = false;
      return_deletes = false;
      row_regexp = 0;
      value_regexp = 0;
      scan_and_filter_rows = false;
      do_not_cache = false;
      and_column_predicates = false;
      debug = nullptr;
    }

    /// Initialize another ScanSpec object with this copy sans the intervals.
    /// This function performs a <i>shallow copy</i> of the column_predicate vector.
    /// If other.column_predicate is destroyed, the pointers of the column
    /// predicates might become invalid.
    void base_copy(ScanSpec &other) const {
      other.row_limit = row_limit;
      other.cell_limit = cell_limit;
      other.cell_limit_per_family = cell_limit_per_family;
      other.row_offset = row_offset;
      other.cell_offset = cell_offset;
      other.max_versions = max_versions;
      other.columns = columns;
      other.time_interval = time_interval;
      other.keys_only = keys_only;
      other.return_deletes = return_deletes;
      other.row_intervals.clear();
      other.cell_intervals.clear();
      other.column_predicates.clear();
      other.row_regexp = row_regexp;
      other.value_regexp = value_regexp;
      other.scan_and_filter_rows = scan_and_filter_rows;
      other.do_not_cache = do_not_cache;
      other.column_predicates = column_predicates;
      other.and_column_predicates = and_column_predicates;
      other.rebuild_indices = rebuild_indices;
      other.debug = debug;
    }

    bool cacheable() const {
      if (do_not_cache || rebuild_indices)
        return false;
      else if (row_intervals.size() == 1) {
        HT_ASSERT(row_intervals[0].start && row_intervals[0].end);
        if (!strcmp(row_intervals[0].start, row_intervals[0].end))
          return true;
      }
      else if (cell_intervals.size() == 1) {
        HT_ASSERT(cell_intervals[0].start_row && cell_intervals[0].end_row);
        if (!strcmp(cell_intervals[0].start_row, cell_intervals[0].end_row))
          return true;
      }
      return false;
    }

    const char *cache_key() const {
      if (!row_intervals.empty())
        return row_intervals[0].start;
      else if (!cell_intervals.empty())
        return cell_intervals[0].start_row;
      HT_ASSERT(!"cache key not found");
      return 0;
    }

    void add_column(CharArena &arena, const string &str) {
      columns.push_back(arena.dup(str));
    }

    /**
     * Parses a column string into column family, qualifier and whether the
     * qualifier is a regexp or not.
     * @param column_str column specified string
     * @param family family name
     * @param qualifier Address of qualifier return pointer
     * @param qualifier_len Address of qualifier return length
     * @param has_qualifier Output parameter set if column_str has qualifer
     * @param is_regexp true if the qualifier string is a regexp
     * @param is_prefix true if the qualifier string is a prefix search
     */
    static void parse_column(const char *column_str, string &family, 
                             const char **qualifier, size_t *qualifier_len,
                             bool *has_qualifier, bool *is_regexp,
                             bool *is_prefix);

    void add_row(CharArena &arena, const string &str) {
      if (cell_intervals.size())
        HT_THROW(Error::BAD_SCAN_SPEC, "cell spec excludes rows");

      RowInterval ri;
      ri.start = ri.end = arena.dup(str);
      ri.start_inclusive = ri.end_inclusive = true;
      row_intervals.push_back(ri);
    }

    void set_row_regexp(CharArena &arena, const char *regexp) {
      if (row_regexp != 0)
        HT_THROWF(Error::BAD_SCAN_SPEC, "row_regexp already set to '%s'", row_regexp);
      row_regexp = arena.dup(regexp);
    }

    void set_value_regexp(CharArena &arena, const char *regexp) {
      if (value_regexp != 0)
        HT_THROWF(Error::BAD_SCAN_SPEC, "value_regexp already set to '%s'", value_regexp);
      value_regexp = arena.dup(regexp);
    }

    void add_row_interval(CharArena &arena,
                          const string &start, bool start_inclusive,
                          const string &end, bool end_inclusive) {
      if (cell_intervals.size())
        HT_THROW(Error::BAD_SCAN_SPEC, "cell spec excludes rows");

      RowInterval ri;
      ri.start = arena.dup(start);
      ri.start_inclusive = start_inclusive;
      ri.end = !end.empty() ? arena.dup(end) : Key::END_ROW_MARKER;
      ri.end_inclusive = end_inclusive;
      row_intervals.push_back(ri);
    }

    void add_cell(CharArena &arena, const string &row, const string &column) {
      if (row_intervals.size())
        HT_THROW(Error::BAD_SCAN_SPEC, "row spec excludes cells");

      CellInterval ci;
      ci.start_row = ci.end_row = arena.dup(row);
      ci.start_column = ci.end_column = arena.dup(column);
      ci.start_inclusive = ci.end_inclusive = true;
      cell_intervals.push_back(ci);
    }

    void add_cell_interval(CharArena &arena,
                           const string &start_row, const string &start_column,
                           bool start_inclusive, const string &end_row,
                           const string &end_column, bool end_inclusive) {
      if (row_intervals.size())
        HT_THROW(Error::BAD_SCAN_SPEC, "row spec excludes cells");

      CellInterval ci;
      ci.start_row = arena.dup(start_row);
      ci.start_column = arena.dup(start_column);
      ci.start_inclusive = start_inclusive;
      ci.end_row = !end_row.empty() ? arena.dup(end_row) : Key::END_ROW_MARKER;
      ci.end_column = arena.dup(end_column);
      ci.end_inclusive = end_inclusive;
      cell_intervals.push_back(ci);
    }

    void add_column_predicate(CharArena &arena, const string &column_family,
                              const char *column_qualifier, uint32_t operation,
                              const char *value, uint32_t value_len = 0) {

      // As soon as we're building with C++11 and can replace the bitset<32> in
      // the CellPredicate class with bitset<64>, then we should change the
      // following expression to check for size of 64
      if (column_predicates.size() == 32)
        HT_THROW(Error::FAILED_EXPECTATION, "Column predicate limit of 32 has been exceeded!");

      ColumnPredicate cp;
      cp.column_family = arena.dup(column_family);
      cp.column_qualifier = arena.dup(column_qualifier);
      cp.column_qualifier_len = column_qualifier ? strlen(column_qualifier) : 0;
      cp.operation = operation;
      if (value) {
        // if value_len have not been specified assume zero terminating string
        cp.value_len = value_len ? value_len : strlen(value);
        cp.value = arena.dup(value, value_len ? value_len : cp.value_len + 1);
      }
      column_predicates.push_back(cp);
    }

    void set_time_interval(int64_t start, int64_t end) {
      time_interval.first = start;
      time_interval.second = end;
    }

    void set_start_time(int64_t start) {
      time_interval.first = start;
    }

    void set_end_time(int64_t end) {
      time_interval.second = end;
    }

    void set_debug(CharArena &arena, const char *deb) {
      if (debug != nullptr)
        HT_THROWF(Error::BAD_SCAN_SPEC, "debug already set to '%s'", debug);
      debug = arena.dup(deb);
    }

    bool debug_equals(const string &str) const {
      return debug && str.compare(debug) == 0;
    }

    bool debug_contains(const string &str) const {
      return debug && strstr(debug, str.c_str());
    }

    int32_t row_limit {};
    int32_t cell_limit {};
    int32_t cell_limit_per_family {};
    int32_t row_offset {};
    int32_t cell_offset {};
    uint32_t max_versions {};
    CstrColumns columns;
    RowIntervals row_intervals;
    CellIntervals cell_intervals;
    ColumnPredicates column_predicates;
    pair<int64_t,int64_t> time_interval;
    const char *row_regexp {};
    const char *value_regexp {};
    bool return_deletes {};
    bool keys_only {};
    bool scan_and_filter_rows {};
    bool do_not_cache {};
    bool and_column_predicates {};
    TableParts rebuild_indices;
    const char *debug {};

  private:

    /// Returns encoding version.
    /// @return Encoding version
    uint8_t encoding_version() const override;

    /// Returns internal serialized length.
    /// @return Internal serialized length
    /// @see encode_internal() for encoding format
    size_t encoded_length_internal() const override;

    /// Writes serialized representation of object to a buffer.
    /// @param bufp Address of destination buffer pointer (advanced by call)
    void encode_internal(uint8_t **bufp) const override;

    /// Reads serialized representation of object from a buffer.
    /// @param version Encoding version
    /// @param bufp Address of destination buffer pointer (advanced by call)
    /// @param remainp Address of integer holding amount of serialized object
    /// remaining
    /// @see encode_internal() for encoding format
    void decode_internal(uint8_t version, const uint8_t **bufp,
			 size_t *remainp) override;

  };

  /**
   * Helper class for building a ScanSpec.  This class manages the allocation
   * of all string members.
   */
  class ScanSpecBuilder : boost::noncopyable {
  public:

    /// Constructor.
    /// @param page_size Page size for memory arena
    ScanSpecBuilder(size_t page_size=8192) :
      m_arena(page_size), m_scan_spec(m_arena) { }

    /// Copy constructor from a ScanSpec.
    /// @param ss ScanSpec to copy
    /// @param page_size Page size for memory arena
    ScanSpecBuilder(const ScanSpec &ss, size_t page_size=8192) :
      m_arena(page_size), m_scan_spec(m_arena, ss) {}

    ScanSpecBuilder &operator=(const ScanSpec &ss) {
      m_scan_spec = ScanSpec(m_arena, ss);
      return *this;
    }

    /**
     * Sets the maximum number of rows to return in the scan.
     *
     * @param n row limit
     */
    void set_row_limit(int32_t n) { m_scan_spec.row_limit = n; }

    /**
     * Sets the maximum number of cells to return
     *
     * @param n cell limit
     */
    void set_cell_limit(int32_t n) { m_scan_spec.cell_limit = n; }

    /**
     * Sets the maximum number of cells to return per column family
     *
     * @param n cell limit per column family
     */
    void set_cell_limit_per_family(int32_t n) { m_scan_spec.cell_limit_per_family = n; }

    /**
     * Sets the number of rows to be skipped at the beginning of the query
     *
     * @param n row offset
     */
    void set_row_offset(int32_t n) { 
      if (n && m_scan_spec.cell_offset)
        HT_THROW(Error::BAD_SCAN_SPEC, "predicate row_offset not allowed in "
                 "combination with cell_offset");
      m_scan_spec.row_offset = n; 
    }

    /**
     * Sets the number of cells to be skipped at the beginning of the query
     *
     * @param n cell offset
     */
    void set_cell_offset(int32_t n) { 
      if (n && m_scan_spec.row_offset)
        HT_THROW(Error::BAD_SCAN_SPEC, "predicate cell_offset not allowed in "
                 "combination with row_offset");
      m_scan_spec.cell_offset = n; 
    }

    /**
     * Sets the maximum number of revisions of each cell to return in the scan.
     *
     * @param n maximum revisions
     */
    void set_max_versions(uint32_t n) { m_scan_spec.max_versions = n; }

    /**
     * Sets the regexp to filter rows by
     *
     * @param regexp row regexp
     */
    void set_row_regexp(const char* regexp) { m_scan_spec.set_row_regexp(m_arena, regexp); }

    /**
     * Sets the regexp to filter cell values by
     *
     * @param regexp value regexp
     */
    void set_value_regexp(const char* regexp) { m_scan_spec.set_value_regexp(m_arena, regexp); }

    /**
     * Adds a column family to be returned by the scan.
     *
     * @param str column family name
     */
    void add_column(const string &str) {
      m_scan_spec.add_column(m_arena, str);
    }

    void reserve_columns(size_t s) { m_scan_spec.columns.reserve(s); }

    /**
     * Adds a column predicate to the scan
     *
     * @param column_family the column family
     * @param column_qualifier Column qualifier
     * @param operation the operation (e.g. ColumnPredicate::EXACT_MATCH or  
     *    ColumnPredicate::PREFIX_MATCH)
     * @param value the cell value
     * @param value_len the length of the value, in bytes. If 0 then 
     *    the length will automatically be assigned using strlen(value)
     */
    void
    add_column_predicate(const string &column_family, const char *column_qualifier,
                         uint32_t operation,  const char *value,
                         uint32_t value_len = 0) {
      m_scan_spec.add_column_predicate(m_arena, column_family, column_qualifier,
                                       operation, value, value_len);
    }

    void reserve_column_predicates(size_t s) { m_scan_spec.column_predicates.reserve(s); }


    /**
     * Adds a row to be returned in the scan
     *
     * @param str row key
     */
    void add_row(const string &str) {
      m_scan_spec.add_row(m_arena, str);
    }

    void reserve_rows(size_t s) { m_scan_spec.row_intervals.reserve(s); }

    /**
     * Adds a row interval to be returned in the scan.
     *
     * @param start start row
     * @param start_inclusive true if interval should include start row
     * @param end end row
     * @param end_inclusive true if interval should include end row
     */
    void add_row_interval(const string &start, bool start_inclusive,
                          const string &end, bool end_inclusive) {
      m_scan_spec.add_row_interval(m_arena, start, start_inclusive,
                                   end, end_inclusive);
    }

    /**
     * Adds a cell to be returned in the scan
     *
     * @param row row key
     * @param column column spec of the form &lt;family&gt;[:&lt;qualifier&gt;]
     */
    void add_cell(const string &row, const string &column) {
      m_scan_spec.add_cell(m_arena, row, column);
    }

    void reserve_cells(size_t s) { m_scan_spec.cell_intervals.reserve(s); }

    /**
     * Adds a cell interval to be returned in the scan.
     *
     * @param start_row start row
     * @param start_column start column spec of the form
     *        &lt;family&gt;[:&lt;qualifier&gt;]
     * @param start_inclusive true if interval should include start row
     * @param end_row end row
     * @param end_column end column spec of the form
     *        &lt;family&gt;[:&lt;qualifier&gt;]
     * @param end_inclusive true if interval should include end row
     */
    void add_cell_interval(const string &start_row, const string &start_column,
                           bool start_inclusive, const string &end_row,
                           const string &end_column, bool end_inclusive) {
      m_scan_spec.add_cell_interval(m_arena, start_row, start_column,
                                    start_inclusive, end_row, end_column, end_inclusive);
    }

    /**
     * Sets the time interval of the scan.  Time values represent number of
     * nanoseconds from 1970-01-00 00:00:00.000000000.
     *
     * @param start start time in nanoseconds
     * @param end end time in nanoseconds
     */
    void set_time_interval(int64_t start, int64_t end) {
      m_scan_spec.set_time_interval(start, end);
    }

    void set_start_time(int64_t start) {
      m_scan_spec.time_interval.first = start;
    }

    void set_end_time(int64_t end) {
      m_scan_spec.time_interval.second = end;
    }

    /**
     * Return only keys (no values)
     */
    void set_keys_only(bool val) {
      m_scan_spec.keys_only = val;
    }

    /**
     * Internal use only.
     */
    void set_return_deletes(bool val) {
      m_scan_spec.return_deletes = val;
    }

    /**
     * Scan and filter rows.
     */
    void set_scan_and_filter_rows(bool val) {
      m_scan_spec.scan_and_filter_rows = val;
    }

    /**
     * Don't cache
     */
    void set_do_not_cache(bool val) {
      m_scan_spec.do_not_cache = val;
    }

    /// Rebuild indices
    /// @param parts Describes which indices to rebuild
    void set_rebuild_indices(TableParts parts) {
      m_scan_spec.rebuild_indices = parts;
    }

    /**
     * AND together the column predicates.
     */
    void set_and_column_predicates(bool val) {
      m_scan_spec.and_column_predicates = val;
    }

    /**
     * Sets the debug string
     *
     * @param debug Debug string
     */
    void set_debug(const char* debug) { m_scan_spec.set_debug(m_arena, debug); }

    /**
     * Clears the state.
     */
    void clear() {
      m_scan_spec.clear();
      // Don't call m_arena.free() here, as for stl containers (vector etc.),
      // clear() assumes underlying storage is still intact!
    }

    /**
     * Returns the built ScanSpec object
     *
     * @return reference to built ScanSpec object
     */
    ScanSpec &get() { return m_scan_spec; }

  private:
    CharArena m_arena;
    ScanSpec m_scan_spec;
  };

  ostream &operator<<(ostream &os, const ScanSpec &scan_spec);

}}

#endif // Hypertable_Lib_ScanSpec_h
