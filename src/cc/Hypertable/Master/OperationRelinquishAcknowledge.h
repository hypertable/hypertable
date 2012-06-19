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

#ifndef HYPERTABLE_OPERATIONRELINQUISHACKNOWLEDGE_H
#define HYPERTABLE_OPERATIONRELINQUISHACKNOWLEDGE_H

#include "Operation.h"

namespace Hypertable {

  class OperationRelinquishAcknowledge : public Operation {
  public:
    OperationRelinquishAcknowledge(ContextPtr &context, EventPtr &event);
    virtual ~OperationRelinquishAcknowledge() { }

    virtual void execute();
    virtual const String name();
    virtual const String label();
    virtual const String graphviz_label();

    virtual void display_state(std::ostream &os);
    virtual size_t encoded_state_length() const;
    virtual void encode_state(uint8_t **bufp) const;
    virtual void decode_state(const uint8_t **bufp, size_t *remainp);
    virtual void decode_request(const uint8_t **bufp, size_t *remainp);

  private:
    String m_source;
    TableIdentifierManaged m_table;
    RangeSpecManaged m_range;
  };

  typedef intrusive_ptr<OperationRelinquishAcknowledge> OperationRelinquishAcknowledgePtr;

} // namespace Hypertable

#endif // HYPERTABLE_OPERATIONRELINQUISHACKNOWLEDGE_H
