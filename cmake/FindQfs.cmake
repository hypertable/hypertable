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

# - Find Qfs
# Find the native Qfs includes and library
#
#  Qfs_INCLUDE_DIR - where to find Kfs.h, etc.
#  Qfs_LIBRARIES   - List of libraries when using Kfs.
#  Qfs_FOUND       - True if Kfs found.


if (Qfs_INCLUDE_DIR)
  # Already in cache, be silent
  set(Qfs_FIND_QUIETLY TRUE)
endif ()

find_path(Qfs_INCLUDE_DIR kfs/KfsClient.h
  /opt/qfs/include
  /opt/local/include
  /usr/local/include
  $ENV{HOME}/src/qfs/build/include
)

macro(FIND_QFS_LIB lib)
  find_library(${lib}_LIB NAMES ${lib}
    PATHS /opt/qfs/lib/static /opt/qfs/lib /opt/local/lib /usr/local/lib
          $ENV{HOME}/src/qfs/build/lib/static
  )
  
  if(${${lib}_LIB})
	mark_as_advanced(${lib}_LIB)
	set(Qfs_LIBRARIES ${Qfs_LIBRARIES} ${${lib}_LIB}})
  endif ()
endmacro(FIND_QFS_LIB lib libname)

FIND_QFS_LIB(qfs_client)
FIND_QFS_LIB(qfs_io)
FIND_QFS_LIB(qfs_common)
FIND_QFS_LIB(qfs_qcdio)
FIND_QFS_LIB(qfs_qcrs)
FIND_QFS_LIB(qfskrb)
FIND_QFS_LIB(Jerasure)
FIND_QFS_LIB(gf_complete)

find_library(Crypto_LIB NAMES crypto PATHS /opt/local/lib /usr/local/lib)

if (Qfs_INCLUDE_DIR AND qfs_client_LIB)
  set(Qfs_FOUND TRUE)
  mark_as_advanced(
    Qfs_INCLUDE_DIR
  )
  message(STATUS "Found QFS: ${Qfs_LIBRARIES}")
else ()
   if (FSBROKER_QFS)
      message(FATAL_ERROR "Could NOT find QFS libraries")
   endif ()
   set(Qfs_FOUND FALSE)
   set(Qfs_LIBRARIES)
endif ()



