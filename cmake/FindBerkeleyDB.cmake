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

# -*- cmake -*-

# - Find BerkeleyDB
# Find the BerkeleyDB includes and library
# This module defines
#  BDB_INCLUDE_DIR, where to find db.h, etc.
#  BDB_LIBRARIES, the libraries needed to use BerkeleyDB.
#  BDB_FOUND, If false, do not try to use BerkeleyDB.
#  also defined, but not for general use are
#  BDB_LIBRARY, where to find the BerkeleyDB library.

find_path(BDB_INCLUDE_DIR db_cxx.h NO_DEFAULT_PATH PATHS
    ${HT_DEPENDENCY_INCLUDE_DIR}
    /usr/local/BerkeleyDB.5.2/include
    /usr/local/BerkeleyDB.4.8/include
    /usr/local/include/db48
    /opt/local/include/db48
    /usr/local/include
    /usr/include/db4.8
    /usr/include/db4
    /usr/include
    )

set(BDB_NAMES ${BDB_NAMES} db_cxx)
find_library(BDB_LIBRARY NAMES ${BDB_NAMES} NO_DEFAULT_PATH PATHS
    ${HT_DEPENDENCY_LIB_DIR}
    /usr/local/BerkeleyDB.5.2/lib
    /usr/local/BerkeleyDB.4.8/lib
    /usr/local/lib/db48
    /usr/lib/i386-linux-gnu
    /usr/lib/x86_64-linux-gnu
    /opt/local/lib/db48
    /usr/local/lib
    /usr/lib
    )

if (BDB_LIBRARY AND BDB_INCLUDE_DIR)
  set(BDB_LIBRARIES ${BDB_LIBRARY})
  set(BDB_FOUND "YES")
else ()
  set(BDB_FOUND "NO")
endif ()

if (NOT BDB_FOUND AND BDB_FIND_REQUIRED)
  message(FATAL_ERROR "Could not find BerkeleyDB library")
endif ()

try_run(BDB_CHECK SHOULD_COMPILE
        ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
        ${HYPERTABLE_SOURCE_DIR}/cmake/CheckBdb.cc
        CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${BDB_INCLUDE_DIR}
                    -DLINK_LIBRARIES=${BDB_LIBRARIES}
        OUTPUT_VARIABLE BDB_TRY_OUT)
string(REGEX REPLACE ".*\n([0-9.]+).*" "\\1" BDB_VERSION ${BDB_TRY_OUT})
string(REGEX REPLACE ".*\n(BerkeleyDB .*)" "\\1" BDB_VERSION ${BDB_VERSION})

  if (NOT BDB_FIND_QUIETLY)
    message(STATUS "Found BerkeleyDB: ${BDB_LIBRARIES}")
    message(STATUS "       version: ${BDB_VERSION}")
  endif ()  

if (NOT BDB_CHECK STREQUAL "0")
  message(FATAL_ERROR "Please fix the Berkeley DB installation, "
          "remove CMakeCache.txt and try again.")
endif ()

STRING(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" "\\1" MAJOR "${BDB_VERSION}")
STRING(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" "\\2" MINOR "${BDB_VERSION}")
STRING(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" "\\3" PATCH "${BDB_VERSION}")

set(BDB_COMPATIBLE "NO")
if (MAJOR MATCHES "^([5-9]|[1-9][0-9]+)" )
  set(BDB_COMPATIBLE "YES")
elseif(MAJOR MATCHES "^4$") 
  if(MINOR MATCHES "^([9]|[1-9][0-9]+)")
    set(BDB_COMPATIBLE "YES")
  elseif(MINOR MATCHES "^8$") 
    if(PATCH MATCHES "^([3-9][0-9]+)")
      set(BDB_COMPATIBLE "YES")
    elseif(PATCH MATCHES "^([2][4-9])")
      set(BDB_COMPATIBLE "YES")
    endif()
  endif()
endif()  

if (NOT BDB_COMPATIBLE)
  message(FATAL_ERROR "BerkeleyDB version >= 4.8.24 required." 
          "Found version ${MAJOR}.${MINOR}.${PATCH}"
          "Please fix the installation, remove CMakeCache.txt and try again.")
endif()

mark_as_advanced(
  BDB_LIBRARY
  BDB_INCLUDE_DIR
  )
