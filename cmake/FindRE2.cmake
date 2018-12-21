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

# - Find RE2 
# Find the native RE2 includes and library
#
#  RE2_INCLUDE_DIR - where to find re2.h, etc.
#  RE2_LIBRARIES   - List of libraries when using RE2.
#  RE2_FOUND       - True if RE2 found.

HT_FASTLIB_SET(
	NAME "RE2" 
	REQUIRED TRUE 
	LIB_PATHS 
	INC_PATHS 
	STATIC libre2.a 
	SHARED re2
	INCLUDE re2/re2.h
)
if (RE2_FOUND)
  try_run(RE2_CHECK RE2_CHECK_BUILD
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckRE2.cc
          CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${RE2_INCLUDE_DIR}
		  LINK_LIBRARIES ${RE2_LIBRARIES}
          OUTPUT_VARIABLE RE2_TRY_OUT)
  if (RE2_CHECK_BUILD AND NOT RE2_CHECK STREQUAL "0")
    string(REGEX REPLACE ".*\n(RE2 .*)" "\\1" RE2_TRY_OUT ${RE2_TRY_OUT})
    message(STATUS "${RE2_TRY_OUT}")
    message(FATAL_ERROR "Please fix the re2 installation and try again.")
    set(RE2_LIBRARIES)
  endif ()
  string(REGEX REPLACE ".*\n([0-9]+[^\n]+).*" "\\1" RE2_VERSION ${RE2_TRY_OUT})
  if (NOT RE2_VERSION MATCHES "^[0-9]+.*")
    set(RE2_VERSION "unknown") 
  endif ()
  message("       version: ${RE2_VERSION}")
endif ()
