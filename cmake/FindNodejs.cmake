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

# - Find Node.js
#  NODEJS_FOUND      - True if node.js found
#  NODEJS_EXECUTABLE - Path to node.js executable

exec_program(env ARGS which node OUTPUT_VARIABLE NODEJS_EXECUTABLE
             RETURN_VALUE NODEJS_RETURN)

if (NODEJS_RETURN STREQUAL "0")

  exec_program(env ARGS ${NODEJS_EXECUTABLE} --version OUTPUT_VARIABLE NODEJS_VERSION_STRING
               RETURN_VALUE NODEJS_RETURN)
  message(STATUS "Node.js Version: ${NODEJS_VERSION_STRING}")
  set(NODEJS_FOUND TRUE)
  
  install(PROGRAMS "${NODEJS_EXECUTABLE}" DESTINATION ${CMAKE_INSTALL_PREFIX}/sbin)
 
else ()
  message(STATUS "Node.js: not found")
  set(NODEJS_FOUND FALSE)
endif ()

mark_as_advanced(CNODEJS_EXECUTABLE)
