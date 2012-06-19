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

#include "MetaLogDefinitionRangeServer.h"

#include "MetaLogEntityRange.h"
#include "MetaLogEntityTaskAcknowledgeRelinquish.h"
#include "MetaLogEntityTaskRemoveTransferLog.h"

using namespace Hypertable;
using namespace Hypertable::MetaLog;

uint16_t DefinitionRangeServer::version() {
  return 2;
}

bool DefinitionRangeServer::supported_version(uint16_t ver) {
  return ver <= 2;
}

const char *DefinitionRangeServer::name() {
  return "rsml";
}

Entity *DefinitionRangeServer::create(uint16_t log_version, const EntityHeader &header) {

  if (header.type == EntityType::RANGE)
    return new EntityRange(header);
  else if (header.type == EntityType::RANGE2)
    return new EntityRange(header);
  else if (header.type == EntityType::TASK_REMOVE_TRANSFER_LOG)
    return new EntityTaskRemoveTransferLog(header);
  else if (header.type == EntityType::TASK_ACKNOWLEDGE_RELINQUISH)
    return new EntityTaskAcknowledgeRelinquish(header);

  HT_THROWF(Error::METALOG_ENTRY_BAD_TYPE,
            "Unrecognized type (%d) encountered in rsml",
            (int)header.type);

}

