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
#  CEPH_INCLUDE_DIR - where to find libceph.h, etc.
#  CEPH_LIBRARIES   - List of libraries when using Ceph.
#  CEPH_FOUND       - True if Ceph found.

HT_FASTLIB_SET(
	NAME "CEPH" 
	SHARED cephfs 
	INCLUDE cephfs/libcephfs.h
)

if (NOT CEPH_FOUND AND FSBROKER_CEPH)
	message(FATAL_ERROR "Could NOT find ceph libraries")
endif ()

