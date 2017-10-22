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

#include "ScanSpec.h"

#include <Hypertable/Lib/KeySpec.h>

#include <Common/Serialization.h>

#include <boost/algorithm/string.hpp>

#include <cstring>
#include <iostream>

using namespace Hypertable;
using namespace Hypertable::Lib;
using namespace std;

uint8_t ScanSpec::encoding_version() const {
  return 2;
}

size_t ScanSpec::encoded_length_internal() const {
  size_t len = Serialization::encoded_length_vi32(row_offset) +
    Serialization::encoded_length_vi32(row_limit) +
    Serialization::encoded_length_vi32(cell_offset) +
    Serialization::encoded_length_vi32(cell_limit) +
    Serialization::encoded_length_vi32(cell_limit_per_family) +
    Serialization::encoded_length_vi32(max_versions) +
    Serialization::encoded_length_vi32(columns.size()) +
    Serialization::encoded_length_vi32(row_intervals.size()) +
    Serialization::encoded_length_vi32(cell_intervals.size()) +
    Serialization::encoded_length_vi32(column_predicates.size()) +
    Serialization::encoded_length_vstr(row_regexp) +
    Serialization::encoded_length_vstr(value_regexp) +
    rebuild_indices.encoded_length();
  for (auto c : columns)
    len += Serialization::encoded_length_vstr(c);
  for (auto &ri : row_intervals)
    len += ri.encoded_length();
  for (auto &ci : cell_intervals)
    len += ci.encoded_length();
  for (auto &cp : column_predicates)
    len += cp.encoded_length();
  len += (8 + 8 + 5);
  // v2
  len += Serialization::encoded_length_vstr(debug);
  return len;
}

/// @details
/// Encoding is as follows:
/// <table>
/// <tr><th>Encoding</th><th>Description</th></tr>
/// <tr><td>i32</td><td>Row offset</td></tr>
/// <tr><td>i32</td><td>Row limit</td></tr>
/// <tr><td>i32</td><td>Cell offset</td></tr>
/// <tr><td>i32</td><td>Cell limit</td></tr>
/// <tr><td>i32</td><td>Cell limit per column family</td></tr>
/// <tr><td>i32</td><td>Max versions</td></tr>
/// <tr><td>i64</td><td>Start time</td></tr>
/// <tr><td>i64</td><td>End time</td></tr>
/// <tr><td>i64</td><td>End time</td></tr>
/// <tr><td>i32</td><td>Column count</td></tr>
/// <tr><td>For each column ...</td></tr>
/// <tr><td>vstr</td><td>Column</td></tr>
/// <tr><td>i32</td><td>Row interval count</td></tr>
/// <tr><td>For each row interval ...</td></tr>
/// <tr><td>RowInterval</td><td>Row interval</td></tr>
/// <tr><td>i32</td><td>Cell interval count</td></tr>
/// <tr><td>For each cell interval ...</td></tr>
/// <tr><td>CellInterval</td><td>Cell interval</td></tr>
/// <tr><td>i32</td><td>Column predicate count</td></tr>
/// <tr><td>For each column predicate ...</td></tr>
/// <tr><td>ColumnPredicate</td><td>Column Preciate</td></tr>
/// <tr><td>vstr</td><td>Row regex</td></tr>
/// <tr><td>vstr</td><td>Value regex</td></tr>
/// <tr><td>bool</td><td><i>return deletes</i> flag</td></tr>
/// <tr><td>bool</td><td><i>keys only</i> flag</td></tr>
/// <tr><td>bool</td><td><i>scan and filter rows</i> flag</td></tr>
/// <tr><td>bool</td><td><i>do not cache</i> flag</td></tr>
/// <tr><td>bool</td><td><i>and column predicates</i> flag</td></tr>
/// <tr><td>verison 2</td></tr>
/// <tr><td>vstr</td><td>Debug string</td></tr>
/// </table>
void ScanSpec::encode_internal(uint8_t **bufp) const {
  Serialization::encode_vi32(bufp, row_offset);
  Serialization::encode_vi32(bufp, row_limit);
  Serialization::encode_vi32(bufp, cell_offset);
  Serialization::encode_vi32(bufp, cell_limit);
  Serialization::encode_vi32(bufp, cell_limit_per_family);
  Serialization::encode_vi32(bufp, max_versions);
  Serialization::encode_i64(bufp, time_interval.first);
  Serialization::encode_i64(bufp, time_interval.second);
  Serialization::encode_vi32(bufp, columns.size());
  for (auto column : columns) Serialization::encode_vstr(bufp, column);
  Serialization::encode_vi32(bufp, row_intervals.size());
  for (auto & ri : row_intervals) ri.encode(bufp);
  Serialization::encode_vi32(bufp, cell_intervals.size());
  for (auto & ci : cell_intervals) ci.encode(bufp);
  Serialization::encode_vi32(bufp, column_predicates.size());
  for (auto & cp : column_predicates) cp.encode(bufp);
  Serialization::encode_vstr(bufp, row_regexp);
  Serialization::encode_vstr(bufp, value_regexp);
  Serialization::encode_bool(bufp, return_deletes);
  Serialization::encode_bool(bufp, keys_only);
  Serialization::encode_bool(bufp, scan_and_filter_rows);
  Serialization::encode_bool(bufp, do_not_cache);
  Serialization::encode_bool(bufp, and_column_predicates);
  rebuild_indices.encode(bufp);
  Serialization::encode_vstr(bufp, debug);
}

void ScanSpec::decode_internal(uint8_t version, const uint8_t **bufp,
                               size_t *remainp) {
  RowInterval ri;
  CellInterval ci;
  ColumnPredicate cp;
  HT_TRY("decoding scan spec",
         row_offset = Serialization::decode_vi32(bufp, remainp);
         row_limit = Serialization::decode_vi32(bufp, remainp);
         cell_offset = Serialization::decode_vi32(bufp, remainp);
         cell_limit = Serialization::decode_vi32(bufp, remainp);
         cell_limit_per_family = Serialization::decode_vi32(bufp, remainp);
         max_versions = Serialization::decode_vi32(bufp, remainp);
         time_interval.first = Serialization::decode_i64(bufp, remainp);
         time_interval.second = Serialization::decode_i64(bufp, remainp);
         for (size_t i = Serialization::decode_vi32(bufp, remainp); i--;)
           columns.push_back(Serialization::decode_vstr(bufp, remainp));
         for (size_t i = Serialization::decode_vi32(bufp, remainp); i--;) {
           ri.decode(bufp, remainp);
           row_intervals.push_back(ri);
         }
         for (size_t i = Serialization::decode_vi32(bufp, remainp); i--;) {
           ci.decode(bufp, remainp);
           cell_intervals.push_back(ci);
         }
         for (size_t i = Serialization::decode_vi32(bufp, remainp); i--;) {
           cp.decode(bufp, remainp);
           column_predicates.push_back(cp);
         }
         row_regexp = Serialization::decode_vstr(bufp, remainp);
         value_regexp = Serialization::decode_vstr(bufp, remainp);
         return_deletes = Serialization::decode_bool(bufp, remainp);
         keys_only = Serialization::decode_bool(bufp, remainp);
         scan_and_filter_rows = Serialization::decode_bool(bufp, remainp);
         do_not_cache = Serialization::decode_bool(bufp, remainp);
         and_column_predicates = Serialization::decode_bool(bufp, remainp);
         rebuild_indices.decode(bufp, remainp));
  if (version >= 2)
    HT_TRY("decoding scan spec (v2)",
           debug = Serialization::decode_vstr(bufp, remainp));
}

const string ScanSpec::render_hql(const string &table) const {
  string hql;

  hql.append("SELECT ");

  if (columns.empty())
    hql.append("*");
  else {
    bool first = true;
    for (auto column : columns) {
      if (first)
        first = false;
      else
        hql.append(",");
      hql.append("\"");
      hql.append(column);
      hql.append("\"");
    }
  }

  hql.append(" FROM ");
  hql.append(table);

  char const *bool_op = " AND ";
  bool first = true;

  // row intervals
  for (auto & ri : row_intervals) {
    if (first) {
      hql.append(" WHERE ");
      first = false;
    }
    else
      hql.append(bool_op);
    hql.append(ri.render_hql());
  }

  if (row_regexp) {
    if (first) {
      hql.append(" WHERE ");
      first = false;
    }
    else
      hql.append(bool_op);
    hql.append(format("ROW REGEXP \"%s\"", row_regexp));
  }

  if (value_regexp) {
    if (first) {
      hql.append(" WHERE ");
      first = false;
    }
    else
      hql.append(bool_op);
    hql.append(format("VALUE REGEXP \"%s\"", value_regexp));
  }


  // cell intervals
  for (auto & ci : cell_intervals) {
    if (first) {
      hql.append(" WHERE ");
      first = false;
    }
    else
      hql.append(bool_op);
    hql.append(ci.render_hql());
  }

  if (!and_column_predicates)
    bool_op = " OR ";

  // column predicates
  for (auto & cp : column_predicates) {
    if (first) {
      hql.append(" WHERE ");
      first = false;
    }
    else
      hql.append(bool_op);
    hql.append(cp.render_hql());
  }
  
  // time interval
  if (time_interval.first != TIMESTAMP_MIN ||
      time_interval.second != TIMESTAMP_MAX) {
    hql.append(" AND ");
    if (time_interval.first != TIMESTAMP_MIN) {
      hql.append(format("%lld", (Lld)time_interval.first));
      hql.append(" <= ");
    }
    hql.append("TIMESTAMP");
    if (time_interval.second != TIMESTAMP_MAX) {
      hql.append(" < ");
      hql.append(format("%lld", (Lld)time_interval.second));
    }
  }

  if (row_offset)
    hql.append(format(" ROW_OFFSET %d", (int)row_offset));

  if (row_limit)
    hql.append(format(" ROW_LIMIT %d", (int)row_limit));

  if (cell_offset)
    hql.append(format(" CELL_OFFSET %d", (int)cell_offset));

  if (cell_limit)
    hql.append(format(" CELL_LIMIT %d", (int)cell_limit));

  if (cell_limit_per_family)
    hql.append(format(" CELL_LIMIT_PER_FAMILY %d", (int)cell_limit_per_family));

  if (max_versions)
    hql.append(format(" MAX_VERSIONS %d", (int)max_versions));

  if (return_deletes)
    hql.append(" RETURN_DELETES");

  if (keys_only)
    hql.append(" KEYS_ONLY");

  if (scan_and_filter_rows)
    hql.append(" SCAN_AND_FILTER_ROWS");

  if (do_not_cache)
    hql.append(" DO_NOT_CACHE");

  if (rebuild_indices)
    hql.append(format(" REBUILD_INDICES %s", rebuild_indices.to_string().c_str()));

  if (debug)
    hql.append(format(" DEBUG \"%s\"", debug));

  return hql;
}


/** @relates ScanSpec */
ostream &Hypertable::Lib::operator<<(ostream &os, const ScanSpec &scan_spec) {
  os <<"{ScanSpec:";

  // columns
  os << " columns=";
  if (scan_spec.columns.empty())
    os << '*';
  else {
    os << '(';
    bool first = true;
    for (auto column : scan_spec.columns) {
      if (first)
        first = false;
      else
        os << ",";
      os << column;
    }
    os <<')';
  }

  // row intervals
  for (auto & ri : scan_spec.row_intervals)
    os << " " << ri;

  // cell intervals
  for (auto & ci : scan_spec.cell_intervals)
    os << " " << ci;

  // column predicates
  for (auto & cp : scan_spec.column_predicates)
    os << " " << cp;
  
  // time interval
  if (scan_spec.time_interval.first != TIMESTAMP_MIN ||
      scan_spec.time_interval.second != TIMESTAMP_MAX) {
    if (scan_spec.time_interval.first != TIMESTAMP_MIN)
      os << scan_spec.time_interval.first << " <= ";
    os << "TIMESTAMP";
    if (scan_spec.time_interval.second != TIMESTAMP_MAX)
      os << " < " << scan_spec.time_interval.second;
  }

  if (scan_spec.row_offset)
    os <<" row_offset=" << scan_spec.row_offset;

  if (scan_spec.row_limit)
    os << " row_limit="<< scan_spec.row_limit;

  if (scan_spec.cell_offset)
    os <<" cell_offset=" << scan_spec.cell_offset;

  if (scan_spec.cell_limit)
    os <<" cell_limit=" << scan_spec.cell_limit;

  if (scan_spec.cell_limit_per_family)
    os << " cell_limit_per_family=" << scan_spec.cell_limit_per_family;

  if (scan_spec.max_versions)
    os << " max_versions=" << scan_spec.max_versions;

  if (scan_spec.return_deletes)
    os << " return_deletes";

  if (scan_spec.keys_only)
    os << " keys_only";

  if (scan_spec.row_regexp)
    os << " row_regexp=" << scan_spec.row_regexp;

  if (scan_spec.value_regexp)
    os << " value_regexp=" << scan_spec.value_regexp;

  if (scan_spec.scan_and_filter_rows)
    os << " scan_and_filter_rows";

  if (scan_spec.do_not_cache)
    os << " do_not_cache";

  if (scan_spec.and_column_predicates)
    os << " and_column_predicates";

  if (scan_spec.rebuild_indices)
    os << " rebuild_indices=" << scan_spec.rebuild_indices.to_string();

  if (scan_spec.debug)
    os << " debug=\"" << scan_spec.debug << "\"";

  os << "}";

  return os;
}


ScanSpec::ScanSpec(CharArena &arena, const ScanSpec &ss)
  : row_limit(ss.row_limit), cell_limit(ss.cell_limit), 
    cell_limit_per_family(ss.cell_limit_per_family),
    row_offset(ss.row_offset), cell_offset(ss.cell_offset),
    max_versions(ss.max_versions), columns(CstrAlloc(arena)), 
    row_intervals(RowIntervalAlloc(arena)),
    cell_intervals(CellIntervalAlloc(arena)),
    column_predicates(ColumnPredicateAlloc(arena)),
    time_interval(ss.time_interval.first, ss.time_interval.second),
    row_regexp(arena.dup(ss.row_regexp)), value_regexp(arena.dup(ss.value_regexp)),
    return_deletes(ss.return_deletes), keys_only(ss.keys_only),
    scan_and_filter_rows(ss.scan_and_filter_rows),
    do_not_cache(ss.do_not_cache), and_column_predicates(ss.and_column_predicates),
    rebuild_indices(ss.rebuild_indices), debug(arena.dup(ss.debug)) {
  columns.reserve(ss.columns.size());
  row_intervals.reserve(ss.row_intervals.size());
  cell_intervals.reserve(ss.cell_intervals.size());
  column_predicates.reserve(ss.column_predicates.size());

  for (auto c : ss.columns)
    add_column(arena, c);

  for (const auto &ri : ss.row_intervals)
    add_row_interval(arena, ri.start, ri.start_inclusive,
                     ri.end, ri.end_inclusive);

  for (const auto &ci : ss.cell_intervals)
    add_cell_interval(arena, ci.start_row, ci.start_column, ci.start_inclusive,
                      ci.end_row, ci.end_column, ci.end_inclusive);

  for (const auto &cp : ss.column_predicates)
    add_column_predicate(arena, cp.column_family, cp.column_qualifier,
                         cp.operation, cp.value, cp.value_len);
}

void
ScanSpec::parse_column(const char *column_str, string &family, 
                       const char **qualifier, size_t *qualifier_len,
                       bool *has_qualifier, bool *is_regexp, bool *is_prefix)
{
  const char *raw_qualifier;
  size_t raw_qualifier_len;
  const char *colon = strchr(column_str, ':');
  *is_regexp = false;
  *is_prefix = false;
  *qualifier = "";
  *qualifier_len = 0;

  if (colon == 0) {
    *has_qualifier = false;
    family = column_str;
    return;
  }
  *has_qualifier = true;

  family = String(column_str, (size_t)(colon-column_str));

  raw_qualifier = colon+1;
  raw_qualifier_len = strlen(raw_qualifier);

  if (raw_qualifier_len == 0)
    return;

  if (raw_qualifier_len > 2 &&
      raw_qualifier[0] == '/' && raw_qualifier[raw_qualifier_len-1] == '/') {
    *is_regexp = true;
    *qualifier = raw_qualifier+1;
    *qualifier_len = raw_qualifier_len - 2;
  }
  else if (*raw_qualifier == '*') {
    *is_prefix = true;
  }
  else {
    if (*raw_qualifier == '^') {
      *is_prefix = true;
      strip_enclosing_quotes(raw_qualifier+1, raw_qualifier_len-1,
                             qualifier, qualifier_len);
    }
    else
      strip_enclosing_quotes(raw_qualifier, raw_qualifier_len,
                             qualifier, qualifier_len);
  }
}
