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

# - Find Ruby Thrift module
# This module defines
#  RUBYTHRIFT_FOUND, If false, do not Ruby w/ thrift

exec_program(env ARGS ruby -I${THRIFT_SOURCE_DIR}/lib/rb/lib -r thrift -e 0 
             OUTPUT_VARIABLE RUBYTHRIFT_OUT
             RETURN_VALUE RUBYTHRIFT_RETURN)

if (RUBYTHRIFT_RETURN STREQUAL "0")
  set(RUBYTHRIFT_FOUND TRUE)
else ()
  set(RUBYTHRIFT_FOUND FALSE)
endif ()

if (RUBYTHRIFT_FOUND)
  if (NOT RUBYTHRIFT_FIND_QUIETLY)
    message(STATUS "Found thrift for ruby, Copying Ruby files into installation")
  endif ()
  
  file(GLOB RUBYFILES ${THRIFT_SOURCE_DIR}/lib/rb/lib/*.rb)
  install(FILES ${RUBYFILES} DESTINATION lib/rb)
  install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/rb/lib/thrift
          DESTINATION lib/rb USE_SOURCE_PERMISSIONS)
else ()
  message(STATUS "Thrift for ruby not found. "
                 "ThriftBroker support for ruby will be disabled")
endif ()

