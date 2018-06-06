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

HT_FASTLIB_SET(
	NAME "SSH" 
	REQUIRED TRUE 
	LIB_PATHS 
	INC_PATHS 
	STATIC libssh.a 
	SHARED ssh
	INCLUDE libssh/libssh.h
)


if (SSH_FOUND)
  exec_program(readelf
               ARGS -s ${SSH_LIBRARIES}
               OUTPUT_VARIABLE LDD_OUT
               RETURN_VALUE LDD_RETURN)
			   
  if (LDD_RETURN STREQUAL "0")
    string(REGEX MATCH "libgcrypt" dummy ${LDD_OUT})
	if(dummy)
		HT_FASTLIB_SET(
			NAME "GCRYPT" 
			REQUIRED TRUE 
			LIB_PATHS 
			INC_PATHS 
			STATIC libgcrypt.a libgpg-error.a
			SHARED gcrypt gpg-error
			INCLUDE gcrypt.h gpg-error.h
		)
		set(SSH_LIBRARIES ${SSH_LIBRARIES} ${GCRYPT_LIBRARIES})
	endif ()
  else()
	set(SSH_LIBRARIES ${SSH_LIBRARIES} ${SSL_LIBRARIES})
  endif ()

  exec_program(${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh
               ARGS ${SSH_LIBRARIES}
               OUTPUT_VARIABLE LDD_OUT
               RETURN_VALUE LDD_RETURN)
  if (LDD_RETURN STREQUAL "0")
    string(REGEX MATCH "[ \t](/[^ ]+/libgssapi_krb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcom_err\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libk5crypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5support\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkeyutils\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssh_LIB_DEPENDENCIES "${Libssh_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
  endif ()

  try_run(TC_CHECK TC_CHECK_BUILD
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckLibssh.cc
          CMAKE_FLAGS 
		  INCLUDE_DIRECTORIES ${SSH_INCLUDE_DIRS}
          LINK_LIBRARIES ${SSH_LIBRARIES}
          RUN_OUTPUT_VARIABLE TC_TRY_OUT)
  if (TC_CHECK_BUILD AND NOT TC_CHECK STREQUAL "0")
    message(STATUS "${TC_TRY_OUT}")
    message(FATAL_ERROR "Please fix the libssh installation and try again.")
  endif ()
  message("       version: ${TC_TRY_OUT}")
  
  if(Libssh_LIB_DEPENDENCIES)
	# Install dependencies
	string(REPLACE " " ";" LIB_DEPENDENCIES_LIST ${Libssh_LIB_DEPENDENCIES})
	foreach(dep ${LIB_DEPENDENCIES_LIST})
		HT_INSTALL_LIBS(lib ${dep})
	endforeach ()
  endif ()
endif ()

