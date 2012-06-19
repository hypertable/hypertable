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
#include "MetaLogEntityTaskRemoveTransferLog.h"

using namespace Hypertable;
using namespace Hypertable::MetaLog;

EntityTaskRemoveTransferLog::EntityTaskRemoveTransferLog(const EntityHeader &header_) : EntityTask(header_) { }

EntityTaskRemoveTransferLog::EntityTaskRemoveTransferLog(const String &log_dir) 
  : EntityTask(EntityType::TASK_REMOVE_TRANSFER_LOG), transfer_log(log_dir) { }

bool EntityTaskRemoveTransferLog::execute() {
  if (Global::user_log)
    Global::user_log->remove_linked_log(transfer_log);
  if (Global::system_log)
    Global::system_log->remove_linked_log(transfer_log);
  if (Global::metadata_log)
    Global::metadata_log->remove_linked_log(transfer_log);
  if (Global::root_log)
    Global::root_log->remove_linked_log(transfer_log);

  try {
    HT_INFOF("Removing transfer log '%s'", transfer_log.c_str());
    if (Global::log_dfs->exists(transfer_log))
      Global::log_dfs->rmdir(transfer_log);
  }
  catch (Exception &e) {
    HT_ERRORF("Problem removing log directory '%s' (%s - %s)",
	      transfer_log.c_str(), Error::get_text(e.code()), e.what());
    return false;
  }
  return true;
}

size_t EntityTaskRemoveTransferLog::encoded_length() const {
  return Serialization::encoded_length_vstr(transfer_log);
}


void EntityTaskRemoveTransferLog::encode(uint8_t **bufp) const {
  Serialization::encode_vstr(bufp, transfer_log);
}

void EntityTaskRemoveTransferLog::decode(const uint8_t **bufp, size_t *remainp) {
  transfer_log = Serialization::decode_vstr(bufp, remainp);
}

const String EntityTaskRemoveTransferLog::name() {
  return String("TaskRemoveTransferLog ") + transfer_log;
}

void EntityTaskRemoveTransferLog::display(std::ostream &os) {
  os << " " << transfer_log << " ";
}

