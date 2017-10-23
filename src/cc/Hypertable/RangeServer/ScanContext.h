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

/// @file
/// Declarations for ScanContext.
/// This file contains the type declarations for the ScanContext, a class that
/// provides context for a scan.

#ifndef Hypertable_RangeServer_ScanContext_h
#define Hypertable_RangeServer_ScanContext_h

#include <Common/ByteString.h>
#include <Common/Error.h>
#include <Common/StringExt.h>

#include <Hypertable/Lib/CellPredicate.h>
#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/RangeSpec.h>
#include <Hypertable/Lib/ScanSpec.h>
#include <Hypertable/Lib/Schema.h>

#include <cassert>
#include <memory>
#include <utility>
#include <set>

namespace Hypertable {

  using namespace std;

  /**
   * Scan context information
   */
  class ScanContext {
  public:
    SchemaPtr schema;
    const ScanSpec *spec;
    ScanSpecBuilder scan_spec_builder;
    const RangeSpec *range;
    RangeSpecManaged range_managed;
    DynamicBuffer dbuf;
    SerializedKey start_serkey, end_serkey;
    Key start_key, end_key;
    String start_row, end_row;
    String start_qualifier, end_qualifier;
    bool start_inclusive, end_inclusive;
    bool single_row {};
    bool use_index {};
    bool has_cell_interval {};
    bool has_start_cf_qualifier {};
    bool restricted_range {};
    int64_t revision;
    pair<int64_t, int64_t> time_interval;
    bool family_mask[256];
    vector<CellPredicate> cell_predicates;
    RE2 *row_regexp;
    RE2 *value_regexp;
    typedef std::set<const char *, LtCstr, CstrAlloc> CstrRowSet;
    CstrRowSet rowset;
    uint32_t timeout_ms;

    /**
     * Constructor.
     *
     * @param rev scan revision
     * @param ss scan specification
     * @param range range specification
     * @param schema smart pointer to schema object
     * @param columns Vector of column identifiers
     */
    ScanContext(int64_t rev, const ScanSpec *ss, const RangeSpec *range,
                SchemaPtr &schema, std::set<uint8_t> *columns=0) :
      cell_predicates(256), row_regexp(0), value_regexp(0), timeout_ms(0) {
      initialize(rev, ss, range, schema, columns);
    }

    /**
     * Constructor.
     *
     * @param rev scan revision
     * @param schema smart pointer to schema object
     */
    ScanContext(int64_t rev, SchemaPtr &schema)
      : cell_predicates(256), row_regexp(0), value_regexp(0), timeout_ms(0) {
      initialize(rev, 0, 0, schema);
    }

    /**
     * Constructor.  Calls initialize() with an empty schema pointer.
     *
     * @param rev scan revision
     */
    ScanContext(int64_t rev=TIMESTAMP_MAX) 
      : cell_predicates(256), row_regexp(0), value_regexp(0), timeout_ms(0) {
      SchemaPtr schema;
      initialize(rev, 0, 0, schema);
    }

    /**
     * Constructor.
     *
     * @param schema smart pointer to schema object
     */
    ScanContext(SchemaPtr &schema) 
      : cell_predicates(256), row_regexp(0), value_regexp(0), timeout_ms(0) {
      initialize(TIMESTAMP_MAX, 0, 0, schema);
    }

    ~ScanContext() {
      if (row_regexp != 0) {
        delete row_regexp;
      }
      if (value_regexp != 0) {
        delete value_regexp;
      }
    }

    void deep_copy_specs() {
      scan_spec_builder = *spec;
      spec = &scan_spec_builder.get();
      range_managed = *range;
      range = &range_managed;
    }

  private:

    /**
     * Initializes the scan context.  Sets up the family_mask filter that
     * allows for quick lookups to see if a family is included in the scan.
     * Also sets up cell_predicates entries for the column families that are
     * included in the scan which contains cell garbage collection info for
     * each family (e.g. cutoff timestamp and number of copies to keep).  Also
     * sets up end_row to be the last possible key in spec->end_row.
     *
     * @param rev scan revision
     * @param ss scan specification
     * @param range range specifier
     * @param sp shared pointer to schema object
     * @param columns Pointer to column family ID set to be populated
     */
    void initialize(int64_t rev, const ScanSpec *ss, const RangeSpec *range,
                    SchemaPtr &sp, std::set<uint8_t> *columns=0);
    /**
     * Disable copy ctor and assignment op
     */
    ScanContext(const ScanContext&);
    ScanContext& operator = (const ScanContext&);

    CharArena arena;
  };

  typedef std::shared_ptr<ScanContext> ScanContextPtr;

}

#endif // Hypertable_RangeServer_ScanContext_h
