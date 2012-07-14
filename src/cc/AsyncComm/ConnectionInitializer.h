/**
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

#ifndef HYPERTABLE_CONNECTIONINITIALIZER_H
#define HYPERTABLE_CONNECTIONINITIALIZER_H

#include "Common/ReferenceCount.h"

#include "CommBuf.h"

namespace Hypertable {

  class Event;

  class ConnectionInitializer : public ReferenceCount {
  public:
    virtual CommBuf *create_initialization_request() = 0;
    virtual bool process_initialization_response(Event *event) = 0;
    virtual uint64_t initialization_command() = 0;
  };
  typedef boost::intrusive_ptr<ConnectionInitializer> ConnectionInitializerPtr;

}

#endif // HYPERTABLE_CONNECTIONINITIALIZER_H
