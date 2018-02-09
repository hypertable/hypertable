/* -*- c++ -*-
 * Copyright (C) 2007-2016 Hypertable, Inc.
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

/// @file
/// Declarations for LocationInitializer.
/// This file contains type declarations for LocationInitializer, a class used
/// to obtain the location string (<i>proxy name</i>) for the range server.

#ifndef Hypertable_RangeServer_LocationInitializer_h
#define Hypertable_RangeServer_LocationInitializer_h

#include <Hypertable/RangeServer/Context.h>

#include <Hypertable/Lib/RangeServer/Protocol.h>

#include <Hyperspace/Session.h>

#include <AsyncComm/ConnectionInitializer.h>

#include <Common/String.h>

#include <condition_variable>
#include <mutex>

namespace Hypertable {

  using namespace Lib;

  /// @addtogroup RangeServer
  /// @{

  /// Obtains location string (<i>proxy name</i>) for the range server.
  class LocationInitializer : public ConnectionInitializer {

  public:

    /// Constructor.
    /// @param context %Range server context
    LocationInitializer(std::shared_ptr<Context> &context);

    /// Checks if "removed" attribute is set on Hyperspace location file
    virtual bool is_removed(const String &path,
                            Hyperspace::SessionPtr &hyperspace);

    CommBuf *create_initialization_request() override;
    bool process_initialization_response(Event *event) override;
    uint64_t initialization_command() override { return Lib::RangeServer::Protocol::COMMAND_INITIALIZE; }

    /// Gets assigned location (proxy name) 
    String get();
    
    /// Remove current location
    bool remove_location();

    /// Waits for completion of initialization handshake
    void wait_for_handshake();

    /// Signals that Hyperspace lock on location file is held
    void set_lock_held() { m_lock_held=true; }

  private:

    /// %Range server context
    std::shared_ptr<Context> m_context;

    /// %Mutex for serializing concurrent access.
    std::mutex m_mutex;

    /// Condition variable signalling completion of initialization handshake
    std::condition_variable m_cond;

    /// Assigned location (proxy name)
    String m_location;

    /// Local pathname to location file
    String m_location_file;

    /// Flag indicating if assigned location has been written to location file
    bool m_location_persisted {};

    /// Flag indicating completion of initialization handshake
    bool m_handshake_complete {};

    /// Flag indicating that Hyperspace lock on location file is held
    bool m_lock_held {};
  };

  /// Shared smart pointer to LocationInitializer
  typedef std::shared_ptr<LocationInitializer> LocationInitializerPtr;

  /// @}
}


#endif // Hypertable_RangeServer_LocationInitializer_h
