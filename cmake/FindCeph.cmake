# Copyright (C) 2007-2012 Hypertable, Inc.
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

find_path(Ceph_INCLUDE cephfs/libcephfs.h
  /usr/include
  $ENV{HOME}/ceph/src/client
)
mark_as_advanced(Ceph_INCLUDE)

find_library(Ceph_LIB
	NAMES cephfs
	PATHS /usr/lib
	      $ENV{HOME}/ceph/src/.libs)
mark_as_advanced(Ceph_LIB)


find_library(SSL_LIB NAMES ssl PATHS 
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
    /opt/local/lib
    /usr/lib
    /lib/x86_64-linux-gnu
    /lib32
    )

mark_as_advanced(SSL_LIB)

if (Ceph_INCLUDE AND Ceph_LIB AND SSL_LIB)
  set(Ceph_FOUND TRUE)
  set(Ceph_LIBRARIES ${Ceph_LIB} ${SSL_LIB})
else ()
   set(Ceph_FOUND FALSE)
   set(Ceph_LIBRARIES)
endif ()

if (Ceph_FOUND)
   message(STATUS "Found ceph: ${Ceph_LIBRARIES}")
else ()
   message(STATUS "Did not find ceph libraries")
   if (Ceph_FIND_REQUIRED)
      message(FATAL_ERROR "Could NOT find ceph libraries (libcephfs & libssl)")
   endif ()
endif ()


