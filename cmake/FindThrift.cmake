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

# - Find Thrift (a cross platform RPC lib/tool)
# This module defines
#  Thrift_VERSION, version string of ant if found
#  Thrift_INCLUDE_DIR, where to find Thrift headers
#  Thrift_LIBS, Thrift libraries
#  Thrift_FOUND, If false, do not try to use ant

exec_program(env ARGS thrift -version OUTPUT_VARIABLE Thrift_VERSION
             RETURN_VALUE Thrift_RETURN)

find_path(Thrift_INCLUDE_DIR Thrift.h NO_DEFAULT_PATH PATHS
  ${HT_DEPENDENCY_INCLUDE_DIR}/thrift
  /usr/local/include/thrift
  /opt/local/include/thrift
  /usr/include/thrift
)

set(Thrift_LIB_PATHS ${HT_DEPENDENCY_LIB_DIR} /usr/local/lib /opt/local/lib /usr/lib64)

find_library(Thrift_LIB NAMES thrift NO_DEFAULT_PATH PATHS ${Thrift_LIB_PATHS})
find_library(Thrift_NB_LIB NAMES thriftnb PATHS ${Thrift_LIB_PATHS})

if (Thrift_VERSION MATCHES "^Thrift version" AND LibEvent_LIBS
    AND LibEvent_INCLUDE_DIR AND Thrift_LIB AND Thrift_NB_LIB
    AND Thrift_INCLUDE_DIR)
  set(Thrift_FOUND TRUE)
  set(Thrift_LIBS ${Thrift_LIB} ${Thrift_NB_LIB})

  exec_program(${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh
               ARGS ${Thrift_LIB}
               OUTPUT_VARIABLE LDD_OUT
               RETURN_VALUE LDD_RETURN)

  if (LDD_RETURN STREQUAL "0")
    string(REGEX MATCH "[ \t](/[^ ]+/libssl\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libgssapi_krb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcom_err\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libk5crypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcrypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5support\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libz\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkeyutils\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
  endif ()

  exec_program(${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh
               ARGS ${Thrift_NB_LIB}
               OUTPUT_VARIABLE LDD_OUT
               RETURN_VALUE LDD_RETURN)

  if (LDD_RETURN STREQUAL "0")
    string(REGEX MATCH "[ \t](/[^ ]+/libssl\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcrypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Thrift_LIB_DEPENDENCIES "${Thrift_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
  endif ()

else ()
  set(Thrift_FOUND FALSE)
  if (NOT LibEvent_LIBS OR NOT LibEvent_INCLUDE_DIR)
    message(STATUS "libevent is required for thrift broker support")
  endif ()
endif ()

if (Thrift_FOUND)
  if (NOT Thrift_FIND_QUIETLY)
    message(STATUS "Found thrift: ${Thrift_LIBS}")
    message(STATUS "    compiler: ${Thrift_VERSION}")
  endif ()
  string(REPLACE "\n" " " Thrift_VERSION ${Thrift_VERSION})
  string(REPLACE " " ";" Thrift_VERSION ${Thrift_VERSION})
  list(GET Thrift_VERSION -1 Thrift_VERSION)
  
else ()
  message(STATUS "Thrift compiler/libraries NOT found. "
          "Thrift support will be disabled (${Thrift_RETURN}, "
          "${Thrift_INCLUDE_DIR}, ${Thrift_LIB}, ${Thrift_NB_LIB})")
endif ()

mark_as_advanced(
  Thrift_LIB
  Thrift_NB_LIB
  Thrift_LIB_DEPENDENCIES
  Thrift_INCLUDE_DIR
  )
