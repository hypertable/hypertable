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

find_package(Tcmalloc)
find_package(Jemalloc)
find_package(Hoard)
# set malloc library (maybe)
if (USE_JEMALLOC)
  if (Jemalloc_FOUND)
    set(MALLOC_LIBRARY ${Jemalloc_LIBRARIES})
	HT_INSTALL_LIBS(lib ${MALLOC_LIBRARY})
  else ()
    message(FATAL_ERROR "Unable to use jemalloc: library not found")
  endif ()
elseif (USE_HOARD)
  if (Hoard_FOUND)
    set(MALLOC_LIBRARY ${Hoard_LIBRARIES})
	HT_INSTALL_LIBS(lib ${MALLOC_LIBRARY})
  else ()
    message(FATAL_ERROR "Unable to use hoard malloc: library not found")
  endif ()
elseif (NOT USE_GLIBC_MALLOC AND TCMALLOC_FOUND)
  set(MALLOC_LIBRARY ${TCMALLOC_LIBRARIES})
  if (TCMALLOC_LIBRARIES MATCHES "tcmalloc_minimal")
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTCMALLOC_MINIMAL")
	SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTCMALLOC_MINIMAL")
  else ()
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTCMALLOC")
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTCMALLOC")
  endif ()
endif ()

# Disable tcmalloc for 32-bit systems
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "i386" OR
    CMAKE_SYSTEM_PROCESSOR STREQUAL "i586" OR
    CMAKE_SYSTEM_PROCESSOR STREQUAL "i686")
  set(MALLOC_LIBRARY "")
else ()
	message(STATUS "Using MALLOC: ${MALLOC_LIBRARY} ")
endif ()
