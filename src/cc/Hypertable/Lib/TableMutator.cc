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

#include "Key.h"
#include "TableMutator.h"

#include <Common/Config.h>
#include <Common/StringExt.h>

#include <boost/algorithm/string.hpp>

#include <chrono>
#include <cstring>
#include <thread>

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace std;

void TableMutator::handle_exceptions() {
  try {
    throw;
  }
  catch (std::bad_alloc &e) {
    set_last_error(Error::BAD_MEMORY_ALLOCATION);
    HT_ERROR("caught bad_alloc here");
  }
  catch (std::exception &e) {
    set_last_error(Error::EXTERNAL);
    HT_ERRORF("caught std::exception: %s", e.what());
  }
  catch (...) {
    set_last_error(Error::EXTERNAL);
    HT_ERROR("caught unknown exception here");
  }
}


TableMutator::TableMutator(PropertiesPtr &props, Comm *comm, Table *table, RangeLocatorPtr &range_locator,
    uint32_t timeout_ms, uint32_t flags)
  : m_callback(this), m_timeout_ms(timeout_ms), m_flags(flags),
    m_last_error(Error::OK), m_stopwatch(true) {
  HT_ASSERT(timeout_ms);
  m_queue = make_shared<TableMutatorQueue>(m_queue_mutex, m_cond);
  ApplicationQueueInterfacePtr app_queue = m_queue;

  m_flush_delay = props->get_i32("Hypertable.Mutator.FlushDelay");
  m_mutator =
    make_shared<TableMutatorAsync>(m_queue_mutex, m_cond, props, comm, app_queue,
                                   table, range_locator, timeout_ms, &m_callback, 
                                   flags, false, this);
}

TableMutator::~TableMutator() {
  try {
    if (m_unflushed_updates)
      flush();
    m_mutator->cancel();
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
  }
}

void
TableMutator::set(const KeySpec &key, const void *value, uint32_t value_len) {

  try {
    m_last_op = SET;
    auto_flush();
    m_mutator->set(key, value, value_len);
    m_unflushed_updates = true;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    save_last(key, value, value_len);
    throw;
  }
  catch (...) {
    handle_exceptions();
    save_last(key, value, value_len);
    throw;
  }
}


void
TableMutator::set_cells(Cells::const_iterator it, Cells::const_iterator end) {

  try {
    m_last_op = SET_CELLS;
    auto_flush();
    m_mutator->set_cells(it, end);
    m_unflushed_updates = true;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    save_last(it, end);
    throw;
  }
  catch (...) {
    handle_exceptions();
    save_last(it, end);
    throw;
  }
}


void TableMutator::set_delete(const KeySpec &key) {

  try {
    m_last_op = SET_DELETE;
    auto_flush();
    m_mutator->set_delete(key);
    m_unflushed_updates = true;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    m_last_key = key;
    throw;
  }
  catch (...) {
    handle_exceptions();
    m_last_key = key;
    throw;
  }
}

void TableMutator::auto_flush() {
  try {

    if (!m_mutator->needs_flush())
      return;

    wait_for_flush_completion(m_mutator.get());

    if (m_flush_delay)
      this_thread::sleep_for(chrono::milliseconds(m_flush_delay));

    m_mutator->flush_with_tablequeue(this,
            !(m_flags & Table::MUTATOR_FLAG_NO_LOG_SYNC));
  }
  catch (...) {
    m_last_op = FLUSH;
    handle_exceptions();
    throw;
  }
}


void TableMutator::flush() {
  try {
    set_last_error(Error::OK);

    wait_for_flush_completion(m_mutator.get());

    m_mutator->flush_with_tablequeue(this);

    wait_for_flush_completion(m_mutator.get());

    m_unflushed_updates = false;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    m_last_op = FLUSH;
    throw;
  }
  catch (...) {
    m_last_op = FLUSH;
    handle_exceptions();
    throw;
  }
}

void TableMutator::wait_for_flush_completion(TableMutatorAsync *mutator) {
  int last_error = 0;
  ApplicationHandler *app_handler = 0;
  while (true) {
    {
      unique_lock<mutex> lock(m_queue_mutex);
      if (mutator->has_outstanding_unlocked()) {
        m_queue->wait_for_buffer(lock, &app_handler);
        {
          lock_guard<mutex> lock(m_mutex);
          last_error = m_last_error;
        }
      }
      else {
        lock_guard<mutex> lock(m_mutex);
        if (m_last_error != Error::OK)
          HT_THROW(m_last_error, "");
        return;
      }
    }
    app_handler->run();
    delete app_handler;

    if (last_error != Error::OK)
      HT_THROW(m_last_error, "");
  }
}

bool TableMutator::retry(uint32_t timeout_ms) {
  uint32_t save_timeout = m_timeout_ms;

  /* issue 938: don't continue if there's a row overflow */
  if (m_last_error == Error::RANGESERVER_ROW_OVERFLOW)
    return true;

  {
    lock_guard<mutex> lock(m_mutex);
    if (m_last_error == Error::OK)
      return true;
    m_last_error = Error::OK;
  }

  try {
    if (timeout_ms != 0)
      m_timeout_ms = timeout_ms;

    switch (m_last_op) {
    case SET:        set(m_last_key, m_last_value, m_last_value_len);   break;
    case SET_DELETE: set_delete(m_last_key);                            break;
    case SET_CELLS:  set_cells(m_last_cells_it, m_last_cells_end);      break;
    case FLUSH:      retry_flush();                                     break;
    }
  }
  catch(...) {
    m_timeout_ms = save_timeout;
    return false;
  }
  m_timeout_ms = save_timeout;
  return true;
}

void TableMutator::retry_flush() {
  FailedMutations failed_mutations;
  CellsBuilderPtr failed_cells;

  {
    lock_guard<mutex> lock(m_mutex);
    if (m_failed_cells && m_failed_cells->size() > 0) {
      failed_mutations.swap(m_failed_mutations);
      failed_cells = m_failed_cells;
      m_failed_cells.reset();
    }
  }

  if (failed_cells && failed_cells->size() > 0)
    set_cells(failed_cells->get());

  this_thread::sleep_for(chrono::milliseconds(2000));

  flush();
}

std::ostream &
TableMutator::show_failed(const Exception &e, std::ostream &out) {
  lock_guard<mutex> lock(m_mutex);

  if (!m_failed_mutations.empty()) {
    for (const auto &v : m_failed_mutations) {
      out << "Failed: (" << v.first.row_key << "," << v.first.column_family;

      if (v.first.column_qualifier && *(v.first.column_qualifier))
        out << ":" << v.first.column_qualifier;

      out << "," << v.first.timestamp << ") - "
          << Error::get_text(v.second) << '\n';
    }
    out.flush();
  }
  else throw e;

  return out;
}

void TableMutator::update_ok() {
  lock_guard<mutex> lock(m_mutex);
  if (m_failed_cells && m_failed_cells->size() > 0) {
    m_failed_mutations.clear();
    m_failed_cells.reset();
  }
}

void TableMutator::update_error(int error, FailedMutations &failures) {
  lock_guard<mutex> lock(m_mutex);
  // copy all failed updates
  m_last_error = error;
  if (!m_failed_cells)
    m_failed_cells = make_shared<CellsBuilder>(failures.size());
  m_failed_cells->copy_failed_mutations(failures, m_failed_mutations);
}
