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

#ifndef HYPERTABLE_METALOGENTITYTASK_H
#define HYPERTABLE_METALOGENTITYTASK_H

#include "Hypertable/Lib/MetaLogEntity.h"

namespace Hypertable {
  namespace MetaLog {

    class EntityTask : public Entity {
    public:
      EntityTask(const EntityHeader &header_) : Entity(header_) { }
      EntityTask(int32_t type) : Entity(type) { }
      virtual ~EntityTask() { }
      virtual const String name() = 0;
      virtual bool execute() = 0;
    };
    typedef intrusive_ptr<EntityTask> EntityTaskPtr;

    namespace EntityType {
      enum {
        TASK_REMOVE_TRANSFER_LOG    = 0x00010003,
        TASK_ACKNOWLEDGE_RELINQUISH = 0x00010004
      };
    }

  } // namespace MetaLog
} // namespace Hypertable

#endif // HYPERTABLE_METALOGENTITYTASK_H
