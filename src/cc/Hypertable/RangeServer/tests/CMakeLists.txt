#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 3
# of the License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Hypertable/RangeServer/tests")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Hypertable/RangeServer/tests")

if (USE_TCMALLOC)
  add_definitions(-DCLEAN_SHUTDOWN)
endif ()

# FileBlockCache test
HT_ADD_TEST(
	NAME FileBlockCache
	SRCS FileBlockCache_test.cc
	TARGETS HyperRanger
)

# QueryCache test
HT_ADD_TEST(
	NAME QueryCache
	SRCS QueryCache_test.cc
	TARGETS HyperRanger
)

# CellStoreScanner test
HT_ADD_TEST(
	NAME CellStoreScanner
	SRCS CellStoreScanner_test.cc
	TARGETS HyperRanger Hypertable
)
configure_file(${SRC_DIR}/CellStoreScanner_test.golden
               ${DST_DIR}/CellStoreScanner_test.golden)
# ${TEST_DEPENDENCIES}

# CellStoreScanner_delete test
HT_ADD_TEST(
	NAME CellStoreScanner-delete
	SRCS CellStoreScanner_delete_test.cc
	TARGETS HyperRanger Hypertable
)
configure_file(${SRC_DIR}/CellStoreScanner_delete_test.golden
               ${DST_DIR}/CellStoreScanner_delete_test.golden)
# ${TEST_DEPENDENCIES}

# AccessGroupGarbageTracker test
HT_ADD_TEST(
	NAME AccessGroup-garbage-tracker
	SRCS AccessGroupGarbageTracker_test.cc
	TARGETS HyperRanger Hypertable
)
#add_executable(AccessGroupGarbageTracker_test AccessGroupGarbageTracker_test.cc)
#target_link_libraries(AccessGroupGarbageTracker_test HyperRanger Hypertable)
#add_test( AccessGroupGarbageTracker_test)

# AccessGroup-hints-file test
HT_ADD_TEST(
	NAME AccessGroup-hints-file
	SRCS access_group_hints_file_test.cc
	TARGETS HyperRanger Hypertable
)

