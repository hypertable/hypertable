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

# - Find SIGAR
# Find the native SIGAR includes and library
#
#  SIGAR_INCLUDE_DIR - where to find SIGAR.h, etc.
#  SIGAR_LIBRARIES   - List of libraries when using SIGAR.
#  SIGAR_FOUND       - True if SIGAR found.


if (SIGAR_INCLUDE_DIR)
  # Already in cache, be silent
  set(SIGAR_FIND_QUIETLY TRUE)
endif ()

find_path(SIGAR_INCLUDE_DIR sigar.h
  /opt/local/include
  /usr/local/include
  /usr/include
)

# SIGAR support a lot more platforms than listed here.
# cf. sigar.hyperic.com
set(SIGAR_NAMES sigar-x86-linux
                sigar-x86_64-linux
                sigar-amd64-linux
		sigar-universal64-macosx
                sigar-universal-macosx
		sigar-x86-solaris
                sigar)
find_library(SIGAR_LIBRARY
  NAMES ${SIGAR_NAMES}
  PATHS /usr/lib /usr/local/lib /opt/local/lib
)

if (SIGAR_INCLUDE_DIR AND SIGAR_LIBRARY)
  set(SIGAR_FOUND TRUE)
  string(STRIP "${SIGAR_LIBRARY}" SIGAR_LIBRARY)
  set(SIGAR_LIBRARIES ${SIGAR_LIBRARY} ${CMAKE_DL_LIBS})
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SYSTEM_VERSION_LINK_LIBS ${SIGAR_LIBRARY} ${CMAKE_DL_LIBS})
  else ()
    set(SYSTEM_VERSION_LINK_LIBS ${SIGAR_LIBRARY})
  endif ()

  try_compile(SYSTEMVERSION_CHECK_BUILD
              ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
              ${HYPERTABLE_SOURCE_DIR}/cmake/SystemVersion.cc
              COPY_FILE ./system_version
              OUTPUT_VARIABLE FOO
              CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${SIGAR_INCLUDE_DIR}
                          "-DLINK_LIBRARIES=${SYSTEM_VERSION_LINK_LIBS}")
  message(STATUS "cb=${SYSTEMVERSION_CHECK_BUILD} c=${SYSTEMVERSION_CHECK} val=${SYSTEMVERSION_TRY_OUT} foo=${FOO}")
  if (NOT SYSTEMVERSION_CHECK_BUILD)
    message(FATAL_ERROR "Unable to determine OS vendor/version")
  endif ()
  execute_process(COMMAND env DYLD_LIBRARY_PATH=/opt/local/lib LD_LIBRARY_PATH=/opt/local/lib ./system_version
                  RESULT_VARIABLE RUN_RESULT
                  OUTPUT_VARIABLE RUN_OUTPUT)
  if (RUN_RESULT STREQUAL "0")
    string(STRIP "${RUN_OUTPUT}" OS_VERSION)
  else ()
    message(FATAL_ERROR "Unable to determine OS vendor/version")
  endif ()
else ()
  set(SIGAR_FOUND FALSE)
  set(SIGAR_LIBRARIES)
endif ()

if (SIGAR_FOUND)
  message(STATUS "Found SIGAR: ${SIGAR_LIBRARIES}")
  message(STATUS "Operating System: ${OS_VERSION}")
else ()
  message(STATUS "Not Found SIGAR: ${SIGAR_LIBRARY}")
  if (SIGAR_FIND_REQUIRED)
    message(STATUS "Looked for SIGAR libraries named ${SIGAR_NAMES}.")
    message(FATAL_ERROR "Could NOT find SIGAR library")
  endif ()
endif ()

mark_as_advanced(
  SIGAR_LIBRARY
  SIGAR_INCLUDE_DIR
  OS_VERSION
)

