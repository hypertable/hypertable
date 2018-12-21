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

# - Find PHP5
# This module defines
#  PHPTHRIFT_FOUND, If false, do not try to use ant

if (THRIFT_SOURCE_DIR AND EXISTS ${THRIFT_SOURCE_DIR}/lib/php/src/Thrift.php)
    set(PHPTHRIFT_FOUND TRUE)
else ()
	set(PHPTHRIFT_FOUND FALSE)
endif ()

if (PHPTHRIFT_FOUND)
  if (NOT PHPTHRIFT_FIND_QUIETLY)
    message(STATUS "Found thrift for PHP, Copying PHP files into installation")
  endif ()
  
  file(GLOB PHPFILES ${HYPERTABLE_SOURCE_DIR}/src/php/*.php)
  install(FILES ${PHPFILES} DESTINATION lib/php)
  install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/php/lib/Thrift
		  DESTINATION lib/php USE_SOURCE_PERMISSIONS)
else ()
  message(STATUS "PHP Thrift files not found. "
                 "ThriftBroker support for php will be disabled")
endif ()

