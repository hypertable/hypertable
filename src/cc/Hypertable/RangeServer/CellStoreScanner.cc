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
/// Declarations for CellStoreScanner.
/// This file contains the type declarations for CellStoreScanner, a class used
/// to scan over (query) some portion of a CellStore.

#include <Common/Compat.h>
#include "CellStoreScanner.h"

#include <Hypertable/RangeServer/CellStoreBlockIndexArray.h>
#include <Hypertable/RangeServer/CellStoreScannerInterval.h>
#include <Hypertable/RangeServer/CellStoreScannerIntervalBlockIndex.h>
#include <Hypertable/RangeServer/CellStoreScannerIntervalReadahead.h>

#include <Hypertable/Lib/BlockHeader.h>

#include <Common/Error.h>
#include <Common/System.h>

using namespace Hypertable;
using namespace std;

template <typename IndexT>
CellStoreScanner<IndexT>::CellStoreScanner(CellStorePtr &&cellstore, ScanContext *scan_ctx, IndexT *index) :
  CellListScanner(scan_ctx), m_cellstore(cellstore), m_decrement_blockindex_refcount(index!=0) {
  SerializedKey start_key, end_key;

  m_keys_only = (scan_ctx->spec) ? (scan_ctx->spec->keys_only && !scan_ctx->spec->value_regexp) : false;

  if (scan_ctx->has_cell_interval) {

    // maybe assert that there isn't more restrictive cellstore start/end row ?

    m_key_buf.grow(4*(scan_ctx->start_key.row_len +
                      scan_ctx->start_key.column_qualifier_len + 32));

    /**
     * Fetch ROW deletes
     */
    start_key.ptr = m_key_buf.ptr;
    create_key_and_append(m_key_buf, FLAG_DELETE_ROW, scan_ctx->start_key.row,
                          0, "", TIMESTAMP_MAX, scan_ctx->revision);

    end_key.ptr = m_key_buf.ptr;
    create_key_and_append(m_key_buf, FLAG_DELETE_COLUMN_FAMILY,
                          scan_ctx->start_key.row, 0, "", TIMESTAMP_MAX,
                          scan_ctx->revision);

    m_interval_scanners[m_interval_max++] =
      make_unique<CellStoreScannerIntervalBlockIndex<IndexT>>(cellstore, index,
                                                              start_key, end_key,
                                                              scan_ctx);

    /**
     * Fetch COLUMN FAMILY deletes
     */
    start_key.ptr = m_key_buf.ptr;
    create_key_and_append(m_key_buf, FLAG_DELETE_COLUMN_FAMILY,
                          scan_ctx->start_key.row,
                          scan_ctx->start_key.column_family_code,
                          "", TIMESTAMP_MAX, scan_ctx->revision);

    end_key.ptr = m_key_buf.ptr;
    create_key_and_append(m_key_buf, FLAG_DELETE_CELL,
                          scan_ctx->start_key.row,
                          scan_ctx->start_key.column_family_code,
                          "", TIMESTAMP_MAX, scan_ctx->revision);

    m_interval_scanners[m_interval_max++] =
      make_unique<CellStoreScannerIntervalBlockIndex<IndexT>>(cellstore, index,
                                                              start_key, end_key,
                                                              scan_ctx);

    if (strcmp(scan_ctx->end_key.row, cellstore->get_end_row()) > 0)
      end_key.ptr = 0;
    else
      end_key.ptr = scan_ctx->end_serkey.ptr;

    if (scan_ctx->use_index)
      m_interval_scanners[m_interval_max++] = make_unique<CellStoreScannerIntervalBlockIndex<IndexT>>(cellstore, index, scan_ctx->start_serkey, end_key, scan_ctx);
    else
      m_interval_scanners[m_interval_max++] = make_unique<CellStoreScannerIntervalReadahead<IndexT>>(cellstore, index, scan_ctx->start_serkey, scan_ctx->end_serkey, scan_ctx);
  }
  else {
    String tmp_str;
    bool readahead = false;
    size_t buf_needed = 128 + strlen(cellstore->get_start_row()) + strlen(cellstore->get_end_row());

    m_key_buf.grow(buf_needed);

    if (strcmp(scan_ctx->start_key.row, cellstore->get_start_row()) < 0) {
      tmp_str = cellstore->get_start_row();
      if (tmp_str.length() > 0)
        tmp_str.append(1, 1);
      create_key_and_append(m_key_buf, 0, tmp_str.c_str(), 0, "", TIMESTAMP_MAX, 0);
      start_key.ptr = m_key_buf.base;
      readahead = true;
    }
    else {
      start_key.ptr = scan_ctx->start_serkey.ptr;
      readahead = scan_ctx->start_key.row_len == 0;
    }

    if (strcmp(scan_ctx->end_key.row, cellstore->get_end_row()) > 0) {
      end_key.ptr = m_key_buf.ptr;
      tmp_str = cellstore->get_end_row();
      tmp_str.append(1, 1);
      create_key_and_append(m_key_buf, 0, tmp_str.c_str(), 0, "", TIMESTAMP_MAX, 0);
      readahead = true;
    }
    else {
      end_key.ptr = scan_ctx->end_serkey.ptr;
      // if !readahead then readahead if scan_ctx->end_key.row == END_ROW_MARKER
      readahead =  readahead || (!strcmp(scan_ctx->end_key.row, Key::END_ROW_MARKER));
    }

    if (scan_ctx->use_index)
      readahead = false;

    if (readahead)
      m_interval_scanners[m_interval_max++] = make_unique<CellStoreScannerIntervalReadahead<IndexT>>(cellstore, index, start_key, end_key, scan_ctx);
    else {
      HT_ASSERT(index);
      m_interval_scanners[m_interval_max++] = make_unique<CellStoreScannerIntervalBlockIndex<IndexT>>(cellstore, index, start_key, end_key, scan_ctx);
    }
  }
}



template <typename IndexT>
CellStoreScanner<IndexT>::~CellStoreScanner() {
  if (m_decrement_blockindex_refcount)
    m_cellstore->decrement_index_refcount();
}



template <typename IndexT>
bool CellStoreScanner<IndexT>::get(Key &key, ByteString &value) {

  if (m_eos)
    return false;

  if (m_interval_scanners[m_interval_index]->get(key, value)) {
    if (m_keys_only)
      value = 0;
    return true;
  }

  m_interval_index++;

  while (m_interval_index < m_interval_max) {
    if (m_interval_scanners[m_interval_index]->get(key, value)) {
      if (m_keys_only)
        value = 0;
      return true;
    }
    m_interval_index++;
  }

  m_eos = true;
  return false;
}

template <typename IndexT>
int64_t CellStoreScanner<IndexT>::get_disk_read() {
  int64_t amount = 0;
  for (size_t i=0; i<m_interval_max; i++)
    amount += m_interval_scanners[i]->get_disk_read();
  return amount;
}



template <typename IndexT>
void CellStoreScanner<IndexT>::forward() {
  if (m_eos)
    return;
  m_interval_scanners[m_interval_index]->forward();
}

namespace Hypertable {
  template class CellStoreScanner<CellStoreBlockIndexArray<uint32_t> >;
  template class CellStoreScanner<CellStoreBlockIndexArray<int64_t> >;
}
