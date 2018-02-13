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

# - Find PYTHON5
# This module defines
#  PYTHONTHRIFT_FOUND, If false, do not try to use ant


set(PYTHONTHRIFT_FOUND OFF)
if (THRIFT_SOURCE_DIR OR EXISTS ${THRIFT_SOURCE_DIR}/lib/py/src/Thrift.py)
	set(PYTHONTHRIFT_FOUND ON)
else ()
	exec_program(env ARGS python -c'import thrift' OUTPUT_VARIABLE PYTHONTHRIFT_OUT
				RETURN_VALUE PYTHONTHRIFT_RETURN)
	if (PYTHONTHRIFT_RETURN STREQUAL "0")
		set(PYTHONTHRIFT_FOUND ON)
	endif ()
endif ()

if (PYTHONTHRIFT_FOUND)

  if (NOT PYTHONTHRIFT_FIND_QUIETLY)
    message(STATUS "Found thrift for python, Copying Python files into installation")
  endif ()
  install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/py/src/
          DESTINATION lib/py/thrift USE_SOURCE_PERMISSIONS)
else ()
  message(STATUS "Thrift for python not found. "
                 "ThriftBroker support for python will be disabled")
endif ()

