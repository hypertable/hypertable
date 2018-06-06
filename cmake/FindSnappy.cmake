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

# - Find Snappy 
# Find the snappy compression library and includes
#
#  SNAPPY_INCLUDE_DIR - where to find snappy.h, etc.
#  SNAPPY_LIBRARIES   - List of libraries when using snappy.
#  SNAPPY_FOUND       - True if snappy found.

HT_FASTLIB_SET(
	NAME "SNAPPY" 
	REQUIRED TRUE 
	LIB_PATHS 
	INC_PATHS 
	STATIC libsnappy.a 
	SHARED snappy
	INCLUDE snappy.h
)
if (SNAPPY_FOUND)
  try_run(SNAPPY_CHECK SNAPPY_CHECK_BUILD
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckSnappy.cc
          CMAKE_FLAGS 
		  INCLUDE_DIRECTORIES ${SNAPPY_INCLUDE_DIR}
          LINK_LIBRARIES ${SNAPPY_LIBRARIES}
          OUTPUT_VARIABLE SNAPPY_TRY_OUT)
  if (SNAPPY_CHECK_BUILD AND NOT SNAPPY_CHECK STREQUAL "0")
    string(REGEX REPLACE ".*\n(SNAPPY .*)" "\\1" SNAPPY_TRY_OUT ${SNAPPY_TRY_OUT})
    message(STATUS "${SNAPPY_TRY_OUT}")
    message(FATAL_ERROR "Please fix the Snappy installation and try again.")
    set(SNAPPY_LIBRARIES)
  endif ()
  string(REGEX REPLACE ".*\n([0-9]+[^\n]+).*" "\\1" SNAPPY_VERSION ${SNAPPY_TRY_OUT})
  if (NOT SNAPPY_VERSION MATCHES "^[0-9]+.*")
    set(SNAPPY_VERSION "unknown") 
  endif ()
  message("       version: ${SNAPPY_VERSION}")
endif ()

