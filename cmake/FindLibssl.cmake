# Copyright (C) 2007-2015 Hypertable, Inc.
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

# - Find Libssl
# Find the libssl includes and library
#
#  Libssl_INCLUDE_DIR      - where to find ssl.h, etc.
#  Libssl_LIBRARIES        - libssl libraries
#  Libssl_LIB_DEPENDENCIES - List of libraries when using libssl.
#  Libssl_FOUND            - True if libssl found.

find_path(Libssl_INCLUDE_DIR openssl/ssl.h NO_DEFAULT_PATH PATHS
  ${HT_DEPENDENCY_INCLUDE_DIR}
  /usr/local/ssl/include
  /usr/include
  /opt/local/include
  /usr/local/include
)

find_library(Libssl_LIBRARY NO_DEFAULT_PATH
  NAMES ssl
  PATHS ${HT_DEPENDENCY_LIB_DIR} /usr/local/ssl/lib /lib /usr/lib/x86_64-linux-gnu/ /lib64 /usr/lib /usr/lib64 /usr/local/lib /usrlocal/lib64 /opt/local/lib
)

find_library(Libcrypto_LIBRARY NO_DEFAULT_PATH
  NAMES crypto
  PATHS ${HT_DEPENDENCY_LIB_DIR} /usr/local/ssl/lib /lib /lib64 /usr/lib /usr/lib64 /usr/local/lib /usrlocal/lib64 /opt/local/lib
)

message(STATUS "Libssl include: ${Libssl_INCLUDE_DIR}")
message(STATUS "Libssl libraries: ${Libssl_LIBRARY} ${Libcrypto_LIBRARY}")

if (Libssl_INCLUDE_DIR AND Libssl_LIBRARY)
  set(Libssl_FOUND TRUE)
  set(Libssl_LIBRARIES ${Libssl_LIBRARY} ${Libcrypto_LIBRARY})

  exec_program(${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh
               ARGS ${Libssl_LIBRARY}
               OUTPUT_VARIABLE LDD_OUT
               RETURN_VALUE LDD_RETURN)

  if (LDD_RETURN STREQUAL "0")
    string(REGEX MATCH "[ \t](/[^ ]+/libssl\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libgssapi_krb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcom_err\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libk5crypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcrypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5support\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libz\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkeyutils\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
  endif ()

else ()
  set(Libssl_FOUND FALSE)
  set(Libssl_LIB_DEPENDENCIES)
  set(Libssl_LIBRARIES)
endif ()

if (Libssl_FOUND)
  message(STATUS "Found Libssl: ${Libssl_LIBRARY}")
  set(TC_TRY_OUT "foo")
  try_run(TC_CHECK TC_CHECK_BUILD
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckLibssl.cc
          CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${Libssl_INCLUDE_DIR}
                      -DLINK_LIBRARIES=${Libssl_LIBRARY}
          RUN_OUTPUT_VARIABLE TC_TRY_OUT)
  if (TC_CHECK_BUILD AND NOT TC_CHECK STREQUAL "0")
    message(STATUS "${TC_TRY_OUT}")
    message(FATAL_ERROR "Please fix the libssl installation and try again.")
  endif ()
  message("       version: ${TC_TRY_OUT}")
else ()
  message(STATUS "Not Found Libssl: ${Libssl_LIBRARY}")
  if (Libssl_FIND_REQUIRED)
    message(FATAL_ERROR "Could NOT find libssl library")
  endif ()
endif ()

mark_as_advanced(
  Libssl_LIBRARIES
  Libssl_LIB_DEPENDENCIES
  Libssl_INCLUDE_DIR
)
