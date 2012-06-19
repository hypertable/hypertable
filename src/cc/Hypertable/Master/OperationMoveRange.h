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

#ifndef HYPERTABLE_OPERATIONMOVERANGE_H
#define HYPERTABLE_OPERATIONMOVERANGE_H

#include "Operation.h"

namespace Hypertable {

  class OperationMoveRange : public Operation {
  public:
    OperationMoveRange(ContextPtr &context, const String &source,
		       const TableIdentifier &table, const RangeSpec &range,
		       const String &transfer_log, uint64_t soft_limit,
		       bool is_split);
    OperationMoveRange(ContextPtr &context, const MetaLog::EntityHeader &header_);
    OperationMoveRange(ContextPtr &context, EventPtr &event);
    virtual ~OperationMoveRange() { }

    void initialize_dependencies();

    virtual void execute();
    virtual const String name();
    virtual const String label();
    virtual const String graphviz_label();
    virtual void display_state(std::ostream &os);
    virtual size_t encoded_state_length() const;
    virtual void encode_state(uint8_t **bufp) const;
    virtual void decode_state(const uint8_t **bufp, size_t *remainp);
    virtual void decode_request(const uint8_t **bufp, size_t *remainp);
    virtual void decode_result(const uint8_t **bufp, size_t *remainp);

    virtual bool remove_explicitly() { return true; }
    virtual int32_t remove_approval_mask() { return 3; }

    String get_location() { return m_destination; }

  private:
    TableIdentifierManaged m_table;
    RangeSpecManaged m_range;
    String m_transfer_log;
    uint64_t m_soft_limit;
    bool m_is_split;
    String m_source;
    String m_destination;
    String m_range_name;
    bool m_in_progress;
  };

  typedef intrusive_ptr<OperationMoveRange> OperationMoveRangePtr;

} // namespace Hypertable

#endif // HYPERTABLE_OPERATIONMOVERANGE_H
