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
add_executable(FileBlockCache_test FileBlockCache_test.cc)
target_link_libraries(FileBlockCache_test HyperRanger)

# QueryCache test
add_executable(QueryCache_test QueryCache_test.cc)
target_link_libraries(QueryCache_test HyperRanger)

# CellStoreScanner test
add_executable(CellStoreScanner_test CellStoreScanner_test.cc
               ${TEST_DEPENDENCIES})
target_link_libraries(CellStoreScanner_test HyperRanger Hypertable)

# CellStoreScanner_delete test
add_executable(CellStoreScanner_delete_test CellStoreScanner_delete_test.cc
               ${TEST_DEPENDENCIES})
target_link_libraries(CellStoreScanner_delete_test HyperRanger Hypertable)

# AccessGroupGarbageTracker test
#add_executable(AccessGroupGarbageTracker_test AccessGroupGarbageTracker_test.cc)
#target_link_libraries(AccessGroupGarbageTracker_test HyperRanger Hypertable)

# AccessGroup-hints-file test
add_executable(access_group_hints_file_test access_group_hints_file_test.cc)
target_link_libraries(access_group_hints_file_test HyperRanger Hypertable)

configure_file(${SRC_DIR}/CellStoreScanner_test.golden
               ${DST_DIR}/CellStoreScanner_test.golden)
configure_file(${SRC_DIR}/CellStoreScanner_delete_test.golden
               ${DST_DIR}/CellStoreScanner_delete_test.golden)

add_test(FileBlockCache FileBlockCache_test)
add_test(QueryCache QueryCache_test)
add_test(CellStoreScanner CellStoreScanner_test)
add_test(CellStoreScanner-bi CellStoreScanner_test use_index)
add_test(CellStoreScanner-delete CellStoreScanner_delete_test)
add_test(CellStoreScanner-delete-bi CellStoreScanner_delete_test use_index)
#add_test(AccessGroup-garbage-tracker AccessGroupGarbageTracker_test)
add_test(AccessGroup-hints-file access_group_hints_file_test)
