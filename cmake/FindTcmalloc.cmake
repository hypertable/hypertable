# Copyright (C) 2007-2012 Hypertable, Inc.
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

# - Find Tcmalloc
# Find the native Tcmalloc includes and library
#
#  Tcmalloc_INCLUDE_DIR - where to find Tcmalloc.h, etc.
#  Tcmalloc_LIBRARIES   - List of libraries when using Tcmalloc.
#  Tcmalloc_FOUND       - True if Tcmalloc found.

find_path(Tcmalloc_INCLUDE_DIR google/tcmalloc.h NO_DEFAULT_PATH PATHS
  ${HT_DEPENDENCY_INCLUDE_DIR}
  /usr/include
  /opt/local/include
  /usr/local/include
)

if (USE_TCMALLOC OR CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(Tcmalloc_NAMES tcmalloc)
else ()
  set(Tcmalloc_NAMES tcmalloc_minimal tcmalloc)
endif ()

find_library(Tcmalloc_LIBRARY NO_DEFAULT_PATH
  NAMES ${Tcmalloc_NAMES}
  PATHS ${HT_DEPENDENCY_LIB_DIR} /lib /usr/lib /usr/local/lib /opt/local/lib
)

find_library(Unwind_LIBRARY NO_DEFAULT_PATH
  NAMES unwind
  PATHS ${HT_DEPENDENCY_LIB_DIR} /lib /usr/lib /usr/local/lib /opt/local/lib
)

if (Tcmalloc_INCLUDE_DIR AND Tcmalloc_LIBRARY)
  set(Tcmalloc_FOUND TRUE)
  set( Tcmalloc_LIBRARIES ${Tcmalloc_LIBRARY} ${Unwind_LIBRARY} )
else ()
  set(Tcmalloc_FOUND FALSE)
  set( Tcmalloc_LIBRARIES )
endif ()

if (Tcmalloc_FOUND)
  message(STATUS "Found Tcmalloc: ${Tcmalloc_LIBRARY}")
  try_run(TC_CHECK TC_CHECK_BUILD
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckTcmalloc.cc
          CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${Tcmalloc_INCLUDE_DIR}
                      -DLINK_LIBRARIES=${Tcmalloc_LIBRARIES}
          RUN_OUTPUT_VARIABLE TC_TRY_OUT)
  #message("tc_check build: ${TC_CHECK_BUILD}")
  #message("tc_check: ${TC_CHECK}")
  #message("tc_version: ${TC_TRY_OUT}")
  if (TC_CHECK_BUILD AND NOT TC_CHECK STREQUAL "0")
    string(REGEX REPLACE ".*\n(Tcmalloc .*)" "\\1" TC_TRY_OUT ${TC_TRY_OUT})
    message(STATUS "${TC_TRY_OUT}")
    message(FATAL_ERROR "Please fix the tcmalloc installation and try again.")
    set(Tcmalloc_LIBRARIES)
  endif ()
  string(REGEX REPLACE ".*\n([0-9]+[^\n]+).*" "\\1" TC_VERSION ${TC_TRY_OUT})
  if (NOT TC_VERSION MATCHES "^[0-9]+.*")
    set(TC_VERSION "unknown -- make sure it's 1.1+")
  endif ()
  message("       version: ${TC_VERSION}")
else ()
  message(STATUS "Not Found Tcmalloc: ${Tcmalloc_LIBRARY}")
  if (Tcmalloc_FIND_REQUIRED)
    message(STATUS "Looked for Tcmalloc libraries named ${Tcmalloc_NAMES}.")
    message(FATAL_ERROR "Could NOT find Tcmalloc library")
  endif ()
endif ()

mark_as_advanced(
  Tcmalloc_LIBRARY
  Tcmalloc_INCLUDE_DIR
  )
