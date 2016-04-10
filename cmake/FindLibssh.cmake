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

# - Find Libssh
# Find the libssh includes and library
#
#  Libssh_INCLUDE_DIR      - where to find libssh.h, etc.
#  Libssh_LIBRARIES        - libssh libraries
#  Libssh_LIB_DEPENDENCIES - List of libraries when using libssh.
#  Libssh_FOUND            - True if libssh found.

find_path(Libssh_INCLUDE_DIR libssh/libssh.h NO_DEFAULT_PATH PATHS
  ${HT_DEPENDENCY_INCLUDE_DIR}
  /usr/include
  /opt/local/include
  /usr/local/include
)

find_library(Libssh_LIBRARY NO_DEFAULT_PATH
  NAMES ssh
  PATHS ${HT_DEPENDENCY_LIB_DIR} /lib /usr/lib /usr/local/lib /opt/local/lib /usr/lib/x86_64-linux-gnu
)

find_library(Libssh_ssl_LIBRARY NO_DEFAULT_PATH
  NAMES ssl
  PATHS ${HT_DEPENDENCY_LIB_DIR} /usr/local/ssl/lib /lib /lib64 /usr/lib /usr/lib64 /usr/local/lib /usrlocal/lib64 /opt/local/lib /usr/lib/x86_64-linux-gnu
)

find_library(Libssh_crypto_LIBRARY NO_DEFAULT_PATH
  NAMES crypto
  PATHS ${HT_DEPENDENCY_LIB_DIR} /usr/local/ssl/lib /lib /lib64 /usr/lib /usr/lib64 /usr/local/lib /usrlocal/lib64 /opt/local/lib
)

if (Libssh_INCLUDE_DIR AND Libssh_LIBRARY)
  set(Libssh_FOUND TRUE)
  set(Libssh_LIBRARIES ${Libssh_LIBRARY} ${Libssh_ssl_LIBRARY} ${Libssh_crypto_LIBRARY})

  exec_program(${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh
               ARGS ${Libssh_LIBRARY}
               OUTPUT_VARIABLE LDD_OUT
               RETURN_VALUE LDD_RETURN)

  if (LDD_RETURN STREQUAL "0")
    string(REGEX MATCH "[ \t](/[^ ]+/libssl\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libgssapi_krb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcom_err\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libk5crypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcrypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5support\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libz\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkeyutils\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
  endif ()

else ()
  set(Libssh_FOUND FALSE)
  set(Libssh_LIB_DEPENDENCIES)
  set(Libssh_LIBRARIES)
endif ()

if (Libssh_FOUND)
  message(STATUS "Found Libssh: ${Libssh_LIBRARIES}")
  message(STATUS "Libssh include: ${Libssh_INCLUDE_DIR}")
  try_run(TC_CHECK TC_CHECK_BUILD
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckLibssh.cc
          CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${Libssh_INCLUDE_DIR}
                      -DLINK_LIBRARIES=${Libssh_LIBRARY}
          RUN_OUTPUT_VARIABLE TC_TRY_OUT)
  if (TC_CHECK_BUILD AND NOT TC_CHECK STREQUAL "0")
    message(STATUS "${TC_TRY_OUT}")
    message(FATAL_ERROR "Please fix the libssl installation and try again.")
  endif ()
  message("       version: ${TC_TRY_OUT}")
else ()
  message(STATUS "Not Found Libssh")
  if (Libssh_FIND_REQUIRED)
    message(FATAL_ERROR "Could NOT find libssh library")
  endif ()
endif ()

mark_as_advanced(
  Libssh_LIBRARIES
  Libssh_LIB_DEPENDENCIES
  Libssh_INCLUDE_DIR
)
