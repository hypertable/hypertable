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
/// Declarations for ConfigHandler.
/// This file contains declarations for ConfigHandler, a dispatch handler class
/// used to check on Config file.

#ifndef Hyperspace_ConfigHandler_h
#define Hyperspace_ConfigHandler_h

#include "Common/Properties.h"
#include <AsyncComm/Comm.h>
#include <AsyncComm/DispatchHandler.h>


namespace Hypertable { namespace Config {

  /// @addtogroup Common
  /// @{

  /// Checks and Reload Configurations.
  /// This class acts as the timer dispatch handler for periodic 
  /// configurations file change check.
  class ConfigHandler : public DispatchHandler {
  public:

    /// Constructor.
    /// Initializes #m_chk_interval to the property
    /// <code>Hypertable.Config.OnFileChange.Reload.Interval</code> 
    /// and reload config file on file change.  Lastly, calls
    /// Comm::set_timer() to register a timer for
    /// #m_chk_interval milliseconds in the future and passes
    /// <code>this</code> as the timer handler.
    /// @param props %Properties object
    ConfigHandler(PropertiesPtr &props);
  
    /// Destructor.
    virtual ~ConfigHandler();

    /// Cancels the timer.
    void stop();

    /// Runs File State Check.
    void run();

    /// Check for file modify time.
    /// Runs m_props->cfg_reload on File Time Change
    /// @param event %Comm layer timer event
    virtual void handle(EventPtr &event);
 
  private:

    /// Properties Ptr
    PropertiesPtr m_props;

    /// Comm layer
    Comm *m_comm;

    /// %Config file check interval
    gInt32tPtr  m_chk_interval;

    /// %Config file check interval value ptr
    gBoolPtr    m_reload;

    /// %Config files with last check timestamp
    std::map<String, time_t> cfg_files;
    typedef std::pair<String, time_t> FilePair;
  };

  /// Smart pointer to ConfigHandler
  typedef std::shared_ptr<ConfigHandler> ConfigHandlerPtr;

  /// @}
}}// namespace Hypertable::Config

#endif // Hyperspace_ConfigHandler_h
