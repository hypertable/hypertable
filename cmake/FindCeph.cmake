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

# - Find Ceph
# Find the native Ceph includes and library
#
#  Ceph_INCLUDE_DIR - where to find libceph.h, etc.
#  Ceph_LIBRARIES   - List of libraries when using Ceph.
#  Ceph_FOUND       - True if Ceph found.


if (Ceph_INCLUDE)
  # Already in cache, be silent
  set(Ceph_FIND_QUIETLY TRUE)
endif ()

find_path(Ceph_INCLUDE ceph/libceph.h
  /usr/local/include
  $ENV{HOME}/ceph/src/client
)

find_library(Ceph_LIB
	NAMES ceph
	PATHS /usr/local/lib
	      $ENV{HOME}/ceph/src/.libs)

if (Ceph_INCLUDE AND Ceph_LIB AND Libssl_LIBRARIES)
  mark_as_advanced(Ceph_INCLUDE)
  mark_as_advanced(Ceph_LIB)
  set(Ceph_FOUND TRUE)
  set(Ceph_LIBRARIES ${Ceph_LIB} ${Libssl_LIBRARIES})
   message(STATUS "Found ceph: ${Ceph_LIBRARIES}")
else ()
   if (FSBROKER_CEPH)
      message(FATAL_ERROR "Could NOT find ceph libraries")
   endif ()
   set(Ceph_FOUND FALSE)
   set(Ceph_LIBRARIES)
endif ()


