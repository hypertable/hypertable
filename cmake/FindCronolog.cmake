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

# - Find Cronolog
# Check if the cronolog binary is installed
#
#  CRONOLOG_DIR        - Directory where cronolog is located
#  CRONOLOG_FOUND      - True if cronolog was found

find_path(CRONOLOG_DIR cronolog
    /usr/bin
    /usr/sbin
    /usr/local/bin
    /usr/local/sbin
    )
if (CRONOLOG_DIR)
  set(CRONOLOG_FOUND TRUE)
endif()

if (CRONOLOG_FOUND)
	message(STATUS "Found cronolog in ${CRONOLOG_DIR}")
	install(PROGRAMS "${CRONOLOG_DIR}/cronolog" DESTINATION sbin)
else ()
	message(FATAL_ERROR "Not Found cronolog")
endif ()

mark_as_advanced(
  CRONOLOG_DIR
  )

