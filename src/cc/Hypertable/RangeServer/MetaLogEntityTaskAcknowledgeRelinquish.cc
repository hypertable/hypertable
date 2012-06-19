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
#include "Common/Serialization.h"
#include "Global.h"
#include "MetaLogEntityTaskAcknowledgeRelinquish.h"

using namespace Hypertable;
using namespace Hypertable::MetaLog;

EntityTaskAcknowledgeRelinquish::EntityTaskAcknowledgeRelinquish(const EntityHeader &header_) : EntityTask(header_) { }

EntityTaskAcknowledgeRelinquish::EntityTaskAcknowledgeRelinquish(const String &loc, const TableIdentifier &t, const RangeSpec &rs)
  : EntityTask(EntityType::TASK_ACKNOWLEDGE_RELINQUISH), location(loc), table(t), range_spec(rs) {
  Global::immovable_range_set_add(table, range_spec);
}

bool EntityTaskAcknowledgeRelinquish::execute() {
  String label = format("%s %s[%s..%s]", location.c_str(),
                        table.id, range_spec.start_row, range_spec.end_row);

  try {
    HT_INFOF("relinquish_acknowledge(%s)", label.c_str());
    Global::master_client->relinquish_acknowledge(location, &table, range_spec);
    Global::immovable_range_set_remove(table, range_spec);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << "Master::relinquish_acknowledge(" << label << ") error - " << e << HT_END;
    return false;
  }

  return true;
}

size_t EntityTaskAcknowledgeRelinquish::encoded_length() const {
  return Serialization::encoded_length_vstr(location) + 
    table.encoded_length() + range_spec.encoded_length();
}


void EntityTaskAcknowledgeRelinquish::encode(uint8_t **bufp) const {
  Serialization::encode_vstr(bufp, location);
  table.encode(bufp);
  range_spec.encode(bufp);
}

void EntityTaskAcknowledgeRelinquish::decode(const uint8_t **bufp, size_t *remainp) {
  location = Serialization::decode_vstr(bufp, remainp);
  table.decode(bufp, remainp);
  range_spec.decode(bufp, remainp);
  Global::immovable_range_set_add(table, range_spec);
}

const String EntityTaskAcknowledgeRelinquish::name() {
  return format("TaskAcknowledgeRelinquish %s[%s..%s]", table.id, range_spec.start_row, range_spec.end_row);
}

void EntityTaskAcknowledgeRelinquish::display(std::ostream &os) {
  os << " " << location << " " << table << " " << range_spec << " ";
}

