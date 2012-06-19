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

#ifndef HYPERTABLE_METALOGENTITYTASKACKNOWLEDGERELINQUISH_H
#define HYPERTABLE_METALOGENTITYTASKACKNOWLEDGERELINQUISH_H

#include "Common/md5.h"

#include "MetaLogEntityTask.h"

namespace Hypertable {
  namespace MetaLog {

    class EntityTaskAcknowledgeRelinquish : public EntityTask {
    public:
      EntityTaskAcknowledgeRelinquish(const EntityHeader &header_);
      EntityTaskAcknowledgeRelinquish(const String &loc, const TableIdentifier &t, const RangeSpec &rs);
      virtual ~EntityTaskAcknowledgeRelinquish() { }
      virtual bool execute();
      virtual size_t encoded_length() const;
      virtual void encode(uint8_t **bufp) const;
      virtual void decode(const uint8_t **bufp, size_t *remainp);
      virtual const String name();
      virtual void display(std::ostream &os);

      String location;
      TableIdentifierManaged table;
      RangeSpecManaged range_spec;
    };

  } // namespace MetaLog
} // namespace Hypertable

#endif // HYPERTABLE_METALOGENTITYTASKACKNOWLEDGERELINQUISH_H
