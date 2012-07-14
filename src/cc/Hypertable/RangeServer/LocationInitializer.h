/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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

#ifndef HYPERTABLE_LOCATIONINITIALIZER_H
#define HYPERTABLE_LOCATIONINITIALIZER_H

#include "Common/String.h"
#include "Common/Mutex.h"

#include "Hypertable/Lib/RangeServerProtocol.h"

#include "AsyncComm/ConnectionInitializer.h"

#include <boost/thread/condition.hpp>

namespace Hypertable {

  class LocationInitializer : public ConnectionInitializer {

  public:
    LocationInitializer(PropertiesPtr &props);
    virtual CommBuf *create_initialization_request();
    virtual bool process_initialization_response(Event *event);
    virtual uint64_t initialization_command() { return RangeServerProtocol::COMMAND_INITIALIZE; }
    String get();
    void wait_until_assigned();

  private:
    Mutex m_mutex;
    boost::condition m_cond;
    PropertiesPtr m_props;
    String m_location;
    String m_location_file;
    bool m_location_persisted;
  };
  typedef boost::intrusive_ptr<LocationInitializer> LocationInitializerPtr;

}


#endif // HYPERTABLE_LOCATIONINITIALIZER_H
