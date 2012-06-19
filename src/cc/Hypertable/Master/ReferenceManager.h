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

#ifndef HYPERTABLE_REFERENCEMANAGER_H
#define HYPERTABLE_REFERENCEMANAGER_H

#include "Common/HashMap.h"

#include "Operation.h"

namespace Hypertable {

  class ReferenceManager {
  public:
    bool add(Operation *operation);
    bool add(OperationPtr &operation) { return add(operation.get()); }
    OperationPtr get(int64_t hash_code);
    void remove(int64_t hash_code);
    void remove(Operation *operation) { remove(operation->hash_code()); }

  private:
    typedef hash_map<int64_t, OperationPtr> ReferenceMapT;
    Mutex m_mutex;
    ReferenceMapT m_map;
  };

} // namespace Hypertable

#endif // HYPERTABLE_REFERENCEMANAGER_H
