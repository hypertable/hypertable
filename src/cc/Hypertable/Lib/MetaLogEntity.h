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

#ifndef HYPERTABLE_METALOGENTITY_H
#define HYPERTABLE_METALOGENTITY_H

#include <iostream>

extern "C" {
#include <time.h>
}

#include <boost/algorithm/string.hpp>

#include "Common/ReferenceCount.h"

#include "MetaLogEntityHeader.h"

namespace Hypertable {

  namespace MetaLog {

    class Reader;
    class Writer;
    
    class Entity : public ReferenceCount {
    public:
      Entity(int32_t type);
      Entity(const EntityHeader &header_);
      virtual ~Entity() { }
      virtual size_t encoded_length() const { return 0; }
      virtual void encode(uint8_t **bufp) const { return; }
      virtual void decode(const uint8_t **bufp, size_t *remainp) { return; }

      void mark_for_removal() { header.flags |= EntityHeader::FLAG_REMOVE; header.length = header.checksum = 0; }
      bool marked_for_removal() { return (header.flags & EntityHeader::FLAG_REMOVE) != 0; }

      virtual const String name() = 0;

      /**
       * Prints a textual representation of the entity state to the given ostream
       *
       * @param os ostream to print entity to
       */
      virtual void display(std::ostream &os) { }

      friend std::ostream &operator <<(std::ostream &os, Entity &entity);
      friend class Reader;
      friend class Writer;

    protected:
      void encode_entry(uint8_t **bufp);
      void decode_entry(const uint8_t **bufp, size_t *remainp);
      EntityHeader header;
    };
    typedef intrusive_ptr<Entity> EntityPtr;

    /**
     * ostream shift function for Entity objects
     */
    inline std::ostream &
    operator <<(std::ostream &os, Entity &entity) {
      os << "{MetaLog::Entity " << entity.name() << " header={";
      entity.header.display(os);
      os << "} payload={";
      entity.display(os);
      os << "}";
      return os;
    }

    namespace EntityType {
      enum {
        RECOVER = 0x00000001
      };
    }

    class EntityRecover : public Entity {
    public:
      EntityRecover() : Entity(EntityType::RECOVER) { }
      virtual const String name() { return "Recover"; }
    };

  }
}

#endif // HYPERTABLE_METALOGENTITY_H
