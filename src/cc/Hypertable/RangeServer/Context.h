/* -*- c++ -*-
 * Copyright (C) 2007-2016 Hypertable, Inc.
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

/// @file
/// Declarations for Context.
/// This file contains type declarations for Context, a class to hold references
/// to commonly needed services and data structures.

#ifndef Hypertable_RangeServer_Context_h
#define Hypertable_RangeServer_Context_h

#include <Hypertable/RangeServer/ServerState.h>
#include <Hypertable/RangeServer/TableInfoMap.h>

#include <AsyncComm/Comm.h>

#include <Common/Properties.h>

#include <memory>

namespace Hypertable {

  /// @addtogroup RangeServer
  /// @{

  /// Holds references to commonly needed services and data structures.
  class RangeServerContext {
  public:
    /// Configuration properties
    PropertiesPtr props;
    /// Comm layer
    Comm *comm {};
    /// Server state
    std::shared_ptr<ServerState> server_state;
    /// Live table info map
    TableInfoMapPtr live_map;
  };

  /// Smart pointer to Context
  typedef std::shared_ptr<RangeServerContext> ContextPtr;

  /// @}

}

#endif // Hypertable_RangeServer_Context_h
