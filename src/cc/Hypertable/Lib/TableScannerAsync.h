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

#ifndef Hypertable_Lib_TableScannerAsync_h
#define Hypertable_Lib_TableScannerAsync_h

#include <Hypertable/Lib/Cells.h>
#include <Hypertable/Lib/CellPredicate.h>
#include <Hypertable/Lib/ClientObject.h>
#include <Hypertable/Lib/ProfileDataScanner.h>
#include <Hypertable/Lib/RangeLocator.h>
#include <Hypertable/Lib/IntervalScannerAsync.h>
#include <Hypertable/Lib/ScanBlock.h>
#include <Hypertable/Lib/Schema.h>
#include <Hypertable/Lib/ResultCallback.h>
#include <Hypertable/Lib/Table.h>

#include <AsyncComm/DispatchHandlerSynchronizer.h>

#include <Common/Stopwatch.h>

#include <condition_variable>
#include <mutex>
#include <vector>

namespace Hypertable {

  class Table;

  /// @addtogroup libHypertable
  /// @{

  /** Asynchronous table scanner. */
  class TableScannerAsync : public ClientObject {

  public:
    /**
     * Constructs a TableScannerAsync object.
     *
     * @param comm pointer to the Comm layer
     * @param app_queue pointer to ApplicationQueueInterface
     * @param table pointer to the table object
     * @param range_locator smart pointer to range locator
     * @param scan_spec reference to scan specification object
     * @param timeout_ms maximum time in milliseconds to allow scanner
     *        methods to execute before throwing an exception
     * @param cb callback to be notified when results arrive
     * @param flags Scanner flags
     */
    TableScannerAsync(Comm *comm, ApplicationQueueInterfacePtr &app_queue, Table *table,
                      RangeLocatorPtr &range_locator,
                      const ScanSpec &scan_spec, uint32_t timeout_ms,
                      ResultCallback *cb, int flags = 0);

    virtual ~TableScannerAsync();

    /**
     * Cancels the scanner
     */
    void cancel();

    bool is_cancelled();

    /**
     *
     */
    bool is_complete() {
      std::unique_lock<std::mutex> lock(m_mutex);
      return m_outstanding == 0;
    }
    
    /**
     * Deal with results of a scanner
     * @param scanner_id id of the scanner which triggered the error
     * @param event event with results
     * @param is_create true if this is event is for a create_scanner request
     */
    void handle_result(int scanner_id, EventPtr &event, bool is_create);

    /**
     * Deal with errors
     *
     * @param scanner_id id of the scanner which triggered the error
     * @param error error code
     * @param error_msg error message
     * @param is_create true if this is event is for a create_scanner request
     */
    void handle_error(int scanner_id, int error, const std::string &error_msg, bool is_create);

    /**
     * Deal with timeouts
     *
     * @param scanner_id id of the scanner which triggered the error
     * @param error_msg error message
     * @param is_create true if this is event is for a create_scanner request
     */
    void handle_timeout(int scanner_id, const std::string &error_msg, bool is_create);

    /**
     * Returns number of bytes scanned
     *
     * @return byte count
     */
    int64_t bytes_scanned() { return m_bytes_scanned; }

    /**
     * Returns the name of the table as it was when the scanner was created
     */
    std::string get_table_name() const;

    /**
     * Returns a pointer to the table
     */
    Table *get_table() {return m_table; }

    /// Gets profile data.
    /// @param profile_data Reference to profile data object populated by this
    /// method
    void get_profile_data(ProfileDataScanner &profile_data) {
      std::unique_lock<std::mutex> lock(m_mutex);
      profile_data = m_profile_data;
    }

  private:
    friend class IndexScannerCallback;

    void init(Comm *comm, ApplicationQueueInterfacePtr &app_queue, Table *table,
            RangeLocatorPtr &range_locator, const ScanSpec &scan_spec, 
            uint32_t timeout_ms, ResultCallback *cb);
    void maybe_callback_ok(int scanner_id, bool next, 
            bool do_callback, ScanCellsPtr &cells);
    void maybe_callback_error(int scanner_id, bool next);
    void wait_for_completion();
    void move_to_next_interval_scanner(int current_scanner);
    bool use_index(Table *table, const ScanSpec &primary_spec, 
                   ScanSpecBuilder &index_spec,
                   std::vector<CellPredicate> &cell_predicates,
                   bool *use_qualifier, bool *row_intervals_applied);
    void transform_primary_scan_spec(ScanSpecBuilder &primary_spec);
    void add_index_row(ScanSpecBuilder &ssb, const char *row);

    std::vector<IntervalScannerAsyncPtr>  m_interval_scanners;
    uint32_t            m_timeout_ms;
    int64_t             m_bytes_scanned {};
    typedef std::set<const char *, LtCstr> CstrRowSet;
    CstrRowSet          m_rowset;
    ResultCallback     *m_cb;
    ProfileDataScanner m_profile_data;
    int                 m_current_scanner {};
    std::mutex m_mutex;
    std::mutex m_cancel_mutex;
    std::condition_variable m_cond;
    int                 m_outstanding {};
    int                 m_error {};
    std::string              m_error_msg;
    Table              *m_table;
    bool                m_cancelled {};
    bool                m_use_index {};
    Stopwatch           m_stopwatch;
  };

  /// Smart pointer to TableScannerAsync
  typedef std::shared_ptr<TableScannerAsync> TableScannerAsyncPtr;

  /// @}

} // namespace Hypertable

#endif // Hypertable_Lib_TableScannerAsync_h
