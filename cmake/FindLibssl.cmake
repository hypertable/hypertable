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

# - Find Libssl
# Find the libssl includes and library
#
#  SSL_INCLUDE_DIR      - where to find ssl.h, etc.
#  SSL_LIBRARIES        - libssl libraries
#  Libssl_LIB_DEPENDENCIES - List of libraries when using libssl.
#  SSL_FOUND            - True if libssl found.


HT_FASTLIB_SET(
	NAME "CRYPTO" 
	REQUIRED TRUE 
	LIB_PATHS /usr/local/ssl/lib
	INC_PATHS /usr/local/ssl/include
	STATIC libcrypto.a 
	SHARED crypto
	INCLUDE openssl/crypto.h
)
set(CRYPTO_LIBRARIES ${CRYPTO_LIBRARIES} dl)

HT_FASTLIB_SET(
	NAME "SSL" 
	REQUIRED TRUE 
	LIB_PATHS /usr/local/ssl/lib
	INC_PATHS /usr/local/ssl/include
	STATIC libssl.a 
	SHARED ssl
	INCLUDE openssl/ssl.h
)


if (SSL_FOUND)
  exec_program(${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh
               ARGS ${SSL_LIBRARIES}
               OUTPUT_VARIABLE LDD_OUT
               RETURN_VALUE LDD_RETURN)

  if (LDD_RETURN STREQUAL "0")
    string(REGEX MATCH "[ \t](/[^ ]+/libgssapi_krb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libcom_err\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libk5crypto\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkrb5support\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
    string(REGEX MATCH "[ \t](/[^ ]+/libkeyutils\\.[^ \n]+)" dummy ${LDD_OUT})
    set(Libssl_LIB_DEPENDENCIES "${Libssl_LIB_DEPENDENCIES} ${CMAKE_MATCH_1}")
  endif ()

  set(TC_TRY_OUT "foo")
  try_run(TC_CHECK TC_CHECK_BUILD
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckLibssl.cc
          CMAKE_FLAGS 
		  INCLUDE_DIRECTORIES ${SSL_INCLUDE_DIRS}
          LINK_LIBRARIES ${SSL_LIBRARIES}
          RUN_OUTPUT_VARIABLE TC_TRY_OUT)
  if (TC_CHECK_BUILD AND NOT TC_CHECK STREQUAL "0")
    message(STATUS "${TC_TRY_OUT}")
    message(FATAL_ERROR "Please fix the libssl installation and try again.")
  endif ()
  message("       version: ${TC_TRY_OUT}")
  
  if(Libssl_LIB_DEPENDENCIES)
	# Install dependencies
	string(REPLACE " " ";" LIB_DEPENDENCIES_LIST ${Libssl_LIB_DEPENDENCIES})
	foreach(dep ${LIB_DEPENDENCIES_LIST})
		HT_INSTALL_LIBS(lib ${dep})
	endforeach ()
  endif ()
endif ()

