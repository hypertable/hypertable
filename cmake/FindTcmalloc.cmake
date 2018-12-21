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

# - Find Tcmalloc
# Find the native Tcmalloc includes and library
#
#  TCMALLOC_INCLUDE_DIRS - where to find Tcmalloc.h, etc.
#  TCMALLOC_LIBRARIES   - List of libraries when using Tcmalloc.
#  Tcmalloc_FOUND       - True if Tcmalloc found.

set(Tcmalloc_NAMES tcmalloc)
set(Tcmalloc_static_NAMES libtcmalloc.a)
set(Tcmalloc_header_NAMES google/tcmalloc.h )

if (NOT USE_TCMALLOC OR NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(Tcmalloc_NAMES tcmalloc_minimal)
  set(Tcmalloc_static_NAMES libtcmalloc_minimal.a)
elseif (CMAKE_SYSTEM_PROCESSOR_x86 AND ${CMAKE_SYSTEM_PROCESSOR_x86} EQUAL 64)
  set(Tcmalloc_NAMES ${Tcmalloc_NAMES} unwind)
  set(Tcmalloc_static_NAMES ${Tcmalloc_static_NAMES} libunwind.a)
  set(Tcmalloc_header_NAMES ${Tcmalloc_header_NAMES} unwind.h)
endif ()

# 
HT_FASTLIB_SET(
	NAME "TCMALLOC" 
	REQUIRED FALSE 
	LIB_PATHS 
	INC_PATHS 
	STATIC ${Tcmalloc_static_NAMES}
	SHARED ${Tcmalloc_NAMES}
	INCLUDE ${Tcmalloc_header_NAMES}
)

if (TCMALLOC_FOUND)
  try_run(TC_CHECK TC_CHECK_BUILD
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckTcmalloc.cc
          CMAKE_FLAGS
		  INCLUDE_DIRECTORIES ${TCMALLOC_INCLUDE_DIRS}
          LINK_LIBRARIES ${TCMALLOC_LIBRARIES}
          RUN_OUTPUT_VARIABLE TC_TRY_OUT)
  #message("tc_check build: ${TC_CHECK_BUILD}")
  #message("tc_check: ${TC_CHECK}")
  #message("tc_version: ${TC_TRY_OUT}")
  if (TC_CHECK_BUILD AND NOT TC_CHECK STREQUAL "0")
    string(REGEX REPLACE ".*\n(Tcmalloc .*)" "\\1" TC_TRY_OUT ${TC_TRY_OUT})
    message(STATUS "${TC_TRY_OUT}")
    message(FATAL_ERROR "Please fix the tcmalloc installation and try again.")
  endif ()
  if(TC_TRY_OUT)
	string(REGEX REPLACE ".*\n([0-9]+[^\n]+).*" "\\1" TC_VERSION ${TC_TRY_OUT})
  endif()
  if (NOT TC_VERSION MATCHES "^[0-9]+.*")
    set(TC_VERSION "unknown -- make sure it's 1.1+")
  endif ()
  message("       version: ${TC_VERSION}")
endif ()

