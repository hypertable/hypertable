# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Hypertable. If not, see <http://www.gnu.org/licenses/>
#

# - Find Rrdtool 
# Check if the rrdtool binary is installed
#
#  RRDTOOL_DIR        - Directory where the rrdtool is located
#  RRDTOOL_FOUND      - True if rrdtool was found

find_path(RRDTOOL_DIR rrdtool
    /usr/bin
    /usr/local/bin
    )
if (RRDTOOL_DIR)
  set(RRDTOOL_FOUND TRUE)
endif()

if (RRDTOOL_FOUND)
  message(STATUS "Found rrdtool in ${RRDTOOL_DIR}")
  
  # if (NOT PACKAGE_THRIFTBROKER)
  #	HT_INSTALL_LIBS(lib ${RRD_LIBRARIES})
  # endif ()
  
  mark_as_advanced(
     RRDTOOL_DIR
  )	
else ()
  message(FATAL_ERROR "Not Found rrdtool")
endif ()

