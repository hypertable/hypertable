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

#ifndef Hypertable_Lib_TableMutator_h
#define Hypertable_Lib_TableMutator_h

#include "Cells.h"
#include "ClientObject.h"
#include "KeySpec.h"
#include "Table.h"
#include "TableMutatorAsync.h"
#include "TableMutatorQueue.h"
#include "TableCallback.h"
#include "RangeLocator.h"
#include "Schema.h"
#include "TableIdentifier.h"

#include <AsyncComm/ConnectionManager.h>

#include <Common/Properties.h>
#include <Common/Stopwatch.h>
#include <Common/StringExt.h>
#include <Common/Timer.h>

#include <condition_variable>
#include <iostream>
#include <mutex>

namespace Hypertable {

  /**
   * Provides the ability to mutate a table in the form of adding and deleting
   * rows and cells.  Objects of this class are used to collect mutations and
   * periodically flush them to the appropriate range servers.  There is a 1 MB
   * buffer of mutations for each range server.  When one of the buffers fills
   * up all the buffers are flushed to their respective range servers.
   */
  class TableMutator : public ClientObject {

  public:
    enum {
      FLAG_NO_LOG_SYNC = Table::MUTATOR_FLAG_NO_LOG_SYNC,
      FLAG_NO_LOG      = Table::MUTATOR_FLAG_NO_LOG
    };

    /**
     * Constructs the TableMutator object
     *
     * @param props reference to properties smart pointer
     * @param comm pointer to the Comm layer
     * @param table pointer to the table object
     * @param range_locator smart pointer to range locator
     * @param timeout_ms maximum time in milliseconds to allow methods
     *        to execute before throwing an exception
     * @param flags rangeserver client update command flags
     */
    TableMutator(PropertiesPtr &props, Comm *comm, Table *table,
                     RangeLocatorPtr &range_locator, uint32_t timeout_ms,
                     uint32_t flags = 0);

    /**
     * Destructor for TableMutator object
     * Make sure buffers are flushed and unsynced rangeservers get synced.
     */
    virtual ~TableMutator();

    /**
     * Inserts a cell into the table.
     *
     * @param key key of the cell being inserted
     * @param value pointer to the value to store in the cell
     * @param value_len length of data pointed to by value
     */
    virtual void set(const KeySpec &key, const void *value, uint32_t value_len);

    /**
     * Convenient helper for null-terminated values
     */
    void set(const KeySpec &key, const char *value) {
      if (value)
        set(key, value, strlen(value));
      else
        set(key, 0, 0);
    }

    /**
     * Convenient helper for std::string values
     */
    void set(const KeySpec &key, const std::string &value) {
      set(key, value.data(), value.length());
    }

    /**
     * Deletes an entire row, a column family in a particular row, or a specific
     * cell within a row.
     *
     * @param key key of the row or cell(s) being deleted
     */
    virtual void set_delete(const KeySpec &key);

    /**
     * Insert a bunch of cells into the table (atomically if cells are in
     * the same range/row)
     *
     * @param cells a list of cells
     */
    virtual void set_cells(const Cells &cells) {
      set_cells(cells.begin(), cells.end());
    }

    /**
     * Flushes the accumulated mutations to their respective range servers.
     */
    virtual void flush();

    /**
     * Retries the last operation
     *
     * @param timeout_ms timeout in milliseconds, 0 means use default timeout
     * @return true if successfully flushed, false otherwise
     */
    virtual bool retry(uint32_t timeout_ms=0);

    /**
     * Checks for failed mutations
     *
     */
    virtual bool need_retry() {
      std::lock_guard<std::mutex> lock(m_mutex);
      return (m_failed_mutations.size() > 0);
    }

    /**
     * Returns the amount of memory used by the collected mutations.
     *
     * @return amount of memory used by the collected mutations.
     */
    virtual uint64_t memory_used() { return m_mutator->memory_used(); }

    /**
     * There are certain circumstances when mutations get flushed to the wrong
     * range server due to stale range location information.  When the correct
     * location information is discovered, these mutations get resent to the
     * proper range server.  This method returns the number of mutations that
     * were resent.
     *
     * @return number of mutations that were resent
     */
    uint64_t get_resend_count() { return m_mutator->get_resend_count(); }

    /**
     * Returns the failed mutations
     *
     * @param failed_mutations reference to vector of Cell/error pairs
     */
    virtual void get_failed(FailedMutations &failed_mutations) {
      std::lock_guard<std::mutex> lock(m_mutex);
      failed_mutations = m_failed_mutations;
    }

    /** Show failed mutations */
    std::ostream &show_failed(const Exception &, std::ostream & = std::cout);

    void update_ok();
    void update_error(int error, FailedMutations &failures);

    int32_t get_last_error() {
      std::lock_guard<std::mutex> lock(m_mutex);
      return m_last_error;
    }

    /** Get elapsed time in seconds
     * @return Elapsed time in seconds
     */
    double elapsed_time() {
      m_stopwatch.stop();
      return m_stopwatch.elapsed();
    }

  private:

    void auto_flush();

    friend class TableMutatorAsync;
    void wait_for_flush_completion(TableMutatorAsync *mutator);

    void set_last_error(int32_t error) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_last_error = error;
    }

    enum Operation {
      SET = 1,
      SET_CELLS,
      SET_DELETE,
      FLUSH
    };

    void save_last(const KeySpec &key, const void *value, size_t value_len) {
      m_last_key = key;
      m_last_value = value;
      m_last_value_len = value_len;
    }

    void save_last(Cells::const_iterator it, Cells::const_iterator end) {
      m_last_cells_it = it;
      m_last_cells_end = end;
    }

    void set_cells(Cells::const_iterator start, Cells::const_iterator end);

    void handle_exceptions();

    void retry_flush();

    std::mutex m_mutex;
    std::mutex m_queue_mutex;
    std::condition_variable m_cond;
    PropertiesPtr m_props;
    Comm                *m_comm;
    TablePtr             m_table;
    SchemaPtr            m_schema;
    RangeLocatorPtr      m_range_locator;
    TableIdentifierManaged m_table_identifier;
    uint64_t             m_memory_used;
    uint64_t             m_max_memory;
    TableCallback m_callback;
    TableMutatorQueuePtr m_queue;
    TableMutatorAsyncPtr m_mutator;
    uint32_t             m_timeout_ms;
    uint32_t             m_flags;
    uint32_t             m_flush_delay {};
    int32_t     m_last_error;
    int         m_last_op {};
    KeySpec     m_last_key;
    const void *m_last_value;
    uint32_t    m_last_value_len;
    Cells::const_iterator m_last_cells_it;
    Cells::const_iterator m_last_cells_end;
    const static uint32_t ms_max_sync_retries = 5;
    bool       m_refresh_schema;
    bool       m_unflushed_updates {};
    FailedMutations m_failed_mutations;
    CellsBuilderPtr m_failed_cells;
    Stopwatch m_stopwatch;
  };

  /// Smart pointer to TableMutator
  typedef std::shared_ptr<TableMutator> TableMutatorPtr;

}

#endif // Hypertable_Lib_TableMutator_h
