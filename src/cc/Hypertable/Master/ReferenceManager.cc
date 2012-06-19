/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
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

#include "ReferenceManager.h"

using namespace Hypertable;


bool ReferenceManager::add(Operation *operation) {
  ScopedLock lock(m_mutex);

  if (m_map.find(operation->hash_code()) != m_map.end())
    return false;
  
  m_map[operation->hash_code()] = operation;
  return true;
}

OperationPtr ReferenceManager::get(int64_t hash_code) {
  ScopedLock lock(m_mutex);
  ReferenceMapT::iterator iter = m_map.find(hash_code);
  if (iter == m_map.end())
    return 0;
  return (*iter).second;
}


void ReferenceManager::remove(int64_t hash_code) {
  ScopedLock lock(m_mutex);
  ReferenceMapT::iterator iter = m_map.find(hash_code);
  if (iter != m_map.end())
    m_map.erase(iter);
}

