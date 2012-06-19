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
#include "Common/Error.h"
#include "Common/FailureInducer.h"
#include "Common/ScopeGuard.h"
#include "Common/Serialization.h"
#include "Common/StringExt.h"
#include "Common/System.h"
#include "Common/md5.h"

#include "OperationMoveRange.h"
#include "OperationProcessor.h"
#include "OperationRelinquishAcknowledge.h"
#include "ReferenceManager.h"
#include "Utility.h"

using namespace Hypertable;

OperationRelinquishAcknowledge::OperationRelinquishAcknowledge(ContextPtr &context, EventPtr &event) 
  : Operation(context, event, MetaLog::EntityType::OPERATION_RELINQUISH_ACKNOWLEDGE) {
  const uint8_t *ptr = event->payload;
  size_t remaining = event->payload_len;
  decode_request(&ptr, &remaining);
  m_dependencies.insert(Dependency::INIT);
}

void OperationRelinquishAcknowledge::execute() {

  HT_INFOF("Entering RelinquishAcknowledge-%lld %s[%s..%s] source=%s state=%s",
           (Lld)header.id, m_table.id, m_range.start_row, m_range.end_row,
           m_source.c_str(), OperationState::get_text(m_state));

  HT_MAYBE_FAIL("relinquish-acknowledge-INITIAL-a");
  HT_MAYBE_FAIL("relinquish-acknowledge-INITIAL-b");

  int64_t hash_code = Utility::range_hash_code(m_table, m_range, String("OperationMoveRange-") + m_source);

  OperationPtr move_op = m_context->reference_manager->get(hash_code);

  if (move_op && move_op->remove_approval_add(0x01)) {
    m_context->reference_manager->remove(hash_code);
    m_context->mml_writer->record_removal(move_op.get());
  }
  else if (!move_op) {
    HT_WARNF("Skipping relinquish_acknowledge(%s %s[%s..%s] because correspoing MoveRange does not exist",
             m_source.c_str(), m_table.id, m_range.start_row, m_range.end_row);
  }

  complete_ok_no_log();
  {
    ScopedLock lock(m_mutex);
    m_expiration_time.reset();
    m_state = OperationState::COMPLETE;
  }

  HT_INFOF("Leaving RelinquishAcknowledge-%lld %s[%s..%s] from %s",
           (Lld)header.id, m_table.id, m_range.start_row, m_range.end_row,
	   m_source.c_str());
}


void OperationRelinquishAcknowledge::display_state(std::ostream &os) {
  os << " " << m_table << " " << m_range << " source=" << m_source;
}

size_t OperationRelinquishAcknowledge::encoded_state_length() const {
  return Serialization::encoded_length_vstr(m_source) + 
    m_table.encoded_length() + m_range.encoded_length();
}

void OperationRelinquishAcknowledge::encode_state(uint8_t **bufp) const {
  Serialization::encode_vstr(bufp, m_source);
  m_table.encode(bufp);
  m_range.encode(bufp);
}

void OperationRelinquishAcknowledge::decode_state(const uint8_t **bufp, size_t *remainp) {
  decode_request(bufp, remainp);
}

void OperationRelinquishAcknowledge::decode_request(const uint8_t **bufp, size_t *remainp) {
  m_source = Serialization::decode_vstr(bufp, remainp);
  m_table.decode(bufp, remainp);
  m_range.decode(bufp, remainp);
}

const String OperationRelinquishAcknowledge::name() {
  return "OperationRelinquishAcknowledge";
}

const String OperationRelinquishAcknowledge::label() {
  return format("RelinquishAcknowledge %s[%s..%s]", m_table.id, m_range.start_row, m_range.end_row);
}

const String OperationRelinquishAcknowledge::graphviz_label() {
  String start_row = m_range.start_row;
  String end_row = m_range.end_row;

  if (start_row.length() > 20)
    start_row = start_row.substr(0, 10) + ".." + start_row.substr(start_row.length()-10, 10);

  if (end_row.length() > 20)
    end_row = end_row.substr(0, 10) + ".." + end_row.substr(end_row.length()-10, 10);
  
  return format("RelinquishAcknowledge %s\\n%s\\n%s", m_table.id, start_row.c_str(), end_row.c_str());
}
