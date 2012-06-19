/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

#include "Common/Compat.h"
#include "Common/Logger.h"

#include "TableInfo.h"

using namespace std;
using namespace Hypertable;


TableInfo::TableInfo(MasterClientPtr &master_client,
                     const TableIdentifier *identifier, SchemaPtr &schema)
    : m_master_client(master_client),
      m_identifier(*identifier), m_schema(schema) {
}


bool TableInfo::remove(const String &start_row, const String &end_row) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(start_row, end_row);
  RangeInfoSet::iterator iter = m_range_set.find(range_info);

  if (iter == m_range_set.end() || !iter->get_range()) {
    HT_INFOF("Unable to remove %s[%s..%s] from TableInfo because non-existant",
             m_identifier.id, start_row.c_str(), end_row.c_str());
    return false;
  }

  HT_INFOF("Removing %s[%s..%s] from TableInfo",
           m_identifier.id, start_row.c_str(), end_row.c_str());

  m_range_set.erase(iter);

  return true;
}


bool
TableInfo::change_end_row(const String &start_row, const String &old_end_row,
                          const String &new_end_row) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(start_row, old_end_row);
  RangeInfoSet::iterator iter = m_range_set.find(range_info);

  if (iter == m_range_set.end() || iter->get_range() == 0) {
    HT_ERRORF("%p: Problem changing old end row '%s' to '%s' (start row '%s')", (void *)this,
              old_end_row.c_str(), new_end_row.c_str(), start_row.c_str());
    for (m_range_set.begin(); iter != m_range_set.end(); ++iter) {
      if (iter->get_range())
        HT_INFOF("%p: [%s..%s] -> %s[%s..%s]", (void *)this,
                 iter->get_start_row().c_str(),
                 iter->get_end_row().c_str(),
                 m_identifier.id,
                 iter->get_range()->start_row().c_str(),
                 iter->get_range()->end_row().c_str());
    }
    return false;
  }
  RangePtr range = iter->get_range();
  range_info.set_range(range);
  range_info.set_end_row(new_end_row);

  HT_INFOF("Changing end row %s removing old row '%s' (start row '%s')",
           m_identifier.id, old_end_row.c_str(), start_row.c_str());

  m_range_set.erase(iter);

  RangeInfoSetInsRec ins = m_range_set.insert(range_info);
  HT_ASSERT(ins.second);

  HT_INFOF("Changing end row %s adding new row '%s' (start row '%s')",
           m_identifier.id, new_end_row.c_str(), start_row.c_str());

  return true;
}

bool
TableInfo::change_start_row(const String &old_start_row, const String &new_start_row,
                            const String &end_row) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(old_start_row, end_row);
  RangeInfoSet::iterator iter = m_range_set.find(range_info);

  if (iter == m_range_set.end()|| !iter->get_range()) {
    HT_ERRORF("%p: Problem changing old start row '%s' to '%s' (end row '%s')", (void *)this,
              old_start_row.c_str(), new_start_row.c_str(), end_row.c_str());
    for (m_range_set.begin(); iter != m_range_set.end(); ++iter) {
      if (iter->get_range())
        HT_INFOF("%p: [%s..%s] -> %s[%s..%s]", (void *)this,
                 iter->get_start_row().c_str(),
                 iter->get_end_row().c_str(),
                 m_identifier.id,
                 iter->get_start_row().c_str(),
                 iter->get_end_row().c_str());
    }
    return false;
  }

  RangePtr range = iter->get_range();
  range_info.set_range(range);
  range_info.set_start_row(new_start_row);

  HT_INFOF("Changing start row %s removing old row '%s' (end row '%s')",
           m_identifier.id, old_start_row.c_str(), end_row.c_str());

  m_range_set.erase(iter);

  RangeInfoSetInsRec ins = m_range_set.insert(range_info);

  HT_ASSERT(ins.second);

  HT_INFOF("Changing start row %s adding new start row '%s' (end row '%s')",
           m_identifier.id, new_start_row.c_str(), end_row.c_str());

  return true;
}

void TableInfo::dump_range_table() {
  ScopedLock lock(m_mutex);
  for (RangeInfoSet::iterator iter = m_range_set.begin();
       iter != m_range_set.end(); ++iter) {
    if (iter->get_range())
      HT_INFOF("%p: [%s..%s] -> %s[%s..%s]", (void *)this,
               iter->get_start_row().c_str(),
               iter->get_end_row().c_str(),
               m_identifier.id,
               iter->get_start_row().c_str(),
               iter->get_end_row().c_str());
  }
}

bool TableInfo::get_range(const RangeSpec *range_spec, RangePtr &range) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(range_spec->start_row, range_spec->end_row);

  RangeInfoSet::iterator iter = m_range_set.find(range_info);

  if (iter == m_range_set.end() || !iter->get_range()) {
    HT_DEBUG_OUT <<"TableInfo couldn't find range ("<< range_spec->start_row << ", "
                 << range_spec->end_row <<")"<< HT_END;

    for (iter = m_range_set.begin(); iter != m_range_set.end(); ++iter)
      HT_DEBUG_OUT <<"TableInfo map: "<< iter->get_start_row() << ".."
                   << iter->get_end_row() << HT_END;

    return false;
  }

  range = iter->get_range();
  return true;
}

bool TableInfo::has_range(const RangeSpec *range_spec) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(range_spec->start_row, range_spec->end_row);

  if (m_range_set.find(range_info) == m_range_set.end())
    return false;
  return true;
}


bool TableInfo::remove_range(const RangeSpec *range_spec, RangePtr &range) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(range_spec->start_row, range_spec->end_row);
  RangeInfoSet::iterator iter = m_range_set.find(range_info);

  if (iter == m_range_set.end() || !iter->get_range()) {
    HT_INFOF("Problem removing range %s[%s..%s] from TableInfo, range not found",
             m_identifier.id, range_spec->start_row, range_spec->end_row);
    return false;
  }

  range = iter->get_range();

  HT_INFOF("Removing range %s[%s..%s] from TableInfo",
           m_identifier.id, range_spec->start_row, range_spec->end_row);

  m_range_set.erase(iter);

  return true;
}


void TableInfo::stage_range(const RangeSpec *range_spec) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(range_spec->start_row, range_spec->end_row);
  RangeInfoSet::iterator iter = m_range_set.find(range_info);
  HT_ASSERT(iter == m_range_set.end());
  HT_INFOF("Staging range %s[%s..%s] to TableInfo",
           m_identifier.id, range_spec->start_row,
           range_spec->end_row);
  RangeInfoSetInsRec ins = m_range_set.insert(range_info);
  HT_ASSERT(ins.second);
}

void TableInfo::unstage_range(const RangeSpec *range_spec) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(range_spec->start_row, range_spec->end_row);
  RangeInfoSet::iterator iter = m_range_set.find(range_info);

  HT_ASSERT(iter != m_range_set.end());
  HT_ASSERT(!iter->get_range());
  HT_INFOF("Unstaging range %s[%s..%s] to TableInfo",
           m_identifier.id, range_spec->start_row,
           range_spec->end_row);
  m_range_set.erase(iter);
}

void TableInfo::add_staged_range(RangePtr &range) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(range->start_row(), range->end_row());
  RangeInfoSet::iterator iter = m_range_set.find(range_info);

  HT_ASSERT(iter != m_range_set.end());
  HT_ASSERT(!iter->get_range());
  HT_INFOF("Adding range %s to TableInfo", range->get_name().c_str());
  range_info.set_range(range);
  m_range_set.erase(iter);
  RangeInfoSetInsRec ins = m_range_set.insert(range_info);
  HT_ASSERT(ins.second);
}


void TableInfo::add_range(RangePtr &range) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info(range->start_row(), range->end_row());
  range_info.set_range(range);
  RangeInfoSet::iterator iter = m_range_set.find(range_info);

  HT_ASSERT(iter == m_range_set.end());
  HT_INFOF("Adding range %s to TableInfo", range->get_name().c_str());
  RangeInfoSetInsRec ins = m_range_set.insert(range_info);
  HT_ASSERT(ins.second);
}

bool
TableInfo::find_containing_range(const String &row, RangePtr &range,
                                 String &start_row, String &end_row) {
  ScopedLock lock(m_mutex);
  RangeInfo range_info((String)"", row);
  RangeInfoSet::iterator iter = m_range_set.lower_bound(range_info);

  if (iter == m_range_set.end())
    return false;

  int num_matches=0;
  RangeInfoSet::iterator tmp = iter;
  for (tmp=tmp; tmp != m_range_set.end() && tmp->get_start_row() < row; tmp++)
    if (tmp->get_range())
      num_matches++;

  for (iter=iter; iter != m_range_set.end() && iter->get_start_row()<row ; iter++) {
    if (iter->get_range() && (num_matches == 1 || !iter->get_range()->get_relinquish())) {
      start_row = iter->get_start_row();
      end_row = iter->get_end_row();
      range = iter->get_range();
      HT_ASSERT(end_row >= row);
      return true;
    }
  }
  return false;
}

bool
TableInfo::find_containing_range(const String &row, RangePtr &range,
                                 const char **start_rowp, const char **end_rowp) const {
  RangeInfo range_info((String)"", row);
  RangeInfoSet::const_iterator iter = m_range_set.lower_bound(range_info);

  if (iter == m_range_set.end())
    return false;

  int num_matches=0;
  RangeInfoSet::const_iterator tmp = iter;
  for (tmp=tmp; tmp != m_range_set.end() && tmp->get_start_row() < row; tmp++)
    if (tmp->get_range())
      num_matches++;

  for (iter=iter; iter != m_range_set.end() && iter->get_start_row()<row ; iter++) {
    range = iter->get_range();
    if (range && (num_matches==1 || !range->get_relinquish())) {
      *start_rowp = range->start_row_cstring();
      *end_rowp = range->end_row_cstring();
      HT_ASSERT(row <= *end_rowp);
      return true;
    }
  }
  range=0;
  return false;
}

bool TableInfo::includes_row(const String &row) const {
  RangeInfo range_info(String(""), row);

  RangeInfoSet::const_iterator iter = m_range_set.lower_bound(range_info);

  if (iter == m_range_set.end())
    return false;
  int num_matches=0;
  RangeInfoSet::const_iterator tmp = iter;
  for (tmp=tmp; tmp != m_range_set.end() && tmp->get_start_row() < row; tmp++)
    if (tmp->get_range())
      num_matches++;

  for (iter=iter; iter != m_range_set.end() && iter->get_start_row() < row; iter++) {
    if (iter->get_range() && (num_matches == 1 || !iter->get_range()->get_relinquish())) {
      HT_ASSERT(row <= iter->get_end_row());
      return true;
    }
  }
  return false;
}

void TableInfo::get_range_vector(std::vector<RangePtr> &range_vec) {
  ScopedLock lock(m_mutex);
  range_vec.reserve(range_vec.size() + m_range_set.size());
  for (RangeInfoSet::iterator iter = m_range_set.begin();
       iter != m_range_set.end(); ++iter) {
    if (iter->get_range())
      range_vec.push_back(iter->get_range());
  }
}


int32_t TableInfo::get_range_count() {
  ScopedLock lock(m_mutex);
  return m_range_set.size();
}


void TableInfo::clear() {
  ScopedLock lock(m_mutex);
  HT_INFOF("Clearing set for table %s",
           m_identifier.id);
  m_range_set.clear();
}

void TableInfo::update_schema(SchemaPtr &schema_ptr) {
  ScopedLock lock(m_mutex);
  // Update individual ranges
  for (RangeInfoSet::iterator iter = m_range_set.begin();
       iter != m_range_set.end(); ++iter) {
    if (iter->get_range())
      iter->get_range()->update_schema(schema_ptr);
  }
  // update table info
  m_schema = schema_ptr;
}
