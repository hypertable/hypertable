#
# Copyright (C) 2007-2012 Hypertable, Inc.
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
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Hypertable/RangeServer")

set(TEST_DEPENDENCIES ${DST_DIR}/words)

set(RangeServer_SRCS
AccessGroup.cc
AccessGroupGarbageTracker.cc
CellCache.cc
CellCacheAllocator.cc
CellCacheManager.cc
CellStoreReleaseCallback.cc
CellCacheScanner.cc
CellStoreFactory.cc
CellStoreScanner.cc
CellStoreScannerIntervalBlockIndex.cc
CellStoreScannerIntervalReadahead.cc
CellStoreTrailerV0.cc
CellStoreTrailerV1.cc
CellStoreTrailerV2.cc
CellStoreTrailerV3.cc
CellStoreTrailerV4.cc
CellStoreTrailerV5.cc
CellStoreTrailerV6.cc
CellStore.cc
CellStoreV0.cc
CellStoreV1.cc
CellStoreV2.cc
CellStoreV3.cc
CellStoreV4.cc
CellStoreV5.cc
CellStoreV6.cc
Config.cc
ConnectionHandler.cc
FileBlockCache.cc
FillScanBlock.cc
Global.cc
GroupCommit.cc
GroupCommitTimerHandler.cc
HyperspaceSessionHandler.cc
IndexUpdater.cc
KeyCompressorNone.cc
KeyCompressorPrefix.cc
KeyDecompressorNone.cc
KeyDecompressorPrefix.cc
LiveFileTracker.cc
LoadMetricsRange.cc
LocationInitializer.cc
MaintenancePrioritizer.cc
MaintenancePrioritizerLogCleanup.cc
MaintenancePrioritizerLowMemory.cc
MaintenanceQueue.cc
MaintenanceScheduler.cc
MaintenanceTaskCompaction.cc
MaintenanceTaskMemoryPurge.cc
MaintenanceTaskRelinquish.cc
MaintenanceTaskSplit.cc
MaintenanceTaskWorkQueue.cc
MergeScanner.cc
MergeScannerRange.cc
MergeScannerAccessGroup.cc
MetaLogEntityRange.cc
MetaLogEntityTaskAcknowledgeRelinquish.cc
MetaLogEntityTaskRemoveTransferLog.cc
MetaLogDefinitionRangeServer.cc
MetadataNormal.cc
MetadataRoot.cc
QueryCache.cc
Range.cc
RangeServer.cc
RangeStatsGatherer.cc
RequestHandlerAcknowledgeLoad.cc
RequestHandlerCompact.cc
RequestHandlerCreateScanner.cc
RequestHandlerDestroyScanner.cc
RequestHandlerDoMaintenance.cc
RequestHandlerDropRange.cc
RequestHandlerDump.cc
RequestHandlerGetStatistics.cc
RequestHandlerGroupCommit.cc
RequestHandlerFetchScanblock.cc
RequestHandlerHeapcheck.cc
RequestHandlerDropTable.cc
RequestHandlerLoadRange.cc
RequestHandlerMetadataSync.cc
RequestHandlerUpdateSchema.cc
RequestHandlerRelinquishRange.cc
RequestHandlerReplayBegin.cc
RequestHandlerReplayLoadRange.cc
RequestHandlerReplayUpdate.cc
RequestHandlerReplayCommit.cc
RequestHandlerStatus.cc
RequestHandlerUpdate.cc
RequestHandlerWaitForMaintenance.cc
RequestHandlerShutdown.cc
RequestHandlerCommitLogSync.cc
ResponseCallbackCreateScanner.cc
ResponseCallbackFetchScanblock.cc
ResponseCallbackGetStatistics.cc
ResponseCallbackUpdate.cc
ScanContext.cc
ScannerMap.cc
TableIdCache.cc
TableInfo.cc
TableInfoMap.cc
TimerHandler.cc
UpdateThread.cc
)

if (USE_TCMALLOC)
  add_definitions(-DCLEAN_SHUTDOWN)
endif ()

# RangeServer Lib
add_library(HyperRanger ${RangeServer_SRCS})
target_link_libraries(HyperRanger m HyperDfsBroker Hypertable ${RE2_LIBRARIES})

# RangeServer
add_executable(Hypertable.RangeServer main.cc)
target_link_libraries(Hypertable.RangeServer HyperRanger ${MALLOC_LIBRARY})

# csdump
add_executable(csdump csdump.cc)
target_link_libraries(csdump HyperRanger)

# count_stored - program to diff two sorted files
add_executable(count_stored count_stored.cc)
target_link_libraries(count_stored HyperRanger)

# FileBlockCache test
add_executable(FileBlockCache_test tests/FileBlockCache_test.cc)
target_link_libraries(FileBlockCache_test HyperRanger)

# QueryCache test
add_executable(QueryCache_test tests/QueryCache_test.cc)
target_link_libraries(QueryCache_test HyperRanger)

# TableIdCache test
add_executable(TableIdCache_test tests/TableIdCache_test.cc)
target_link_libraries(TableIdCache_test HyperRanger)

# CellStoreScanner tests
add_executable(CellStoreScanner_test tests/CellStoreScanner_test.cc
               ${TEST_DEPENDENCIES})
target_link_libraries(CellStoreScanner_test HyperRanger Hypertable)

add_executable(CellStoreScanner_delete_test tests/CellStoreScanner_delete_test.cc
               ${TEST_DEPENDENCIES})
target_link_libraries(CellStoreScanner_delete_test HyperRanger Hypertable)

# 64-bit CellStore test
add_executable(CellStore64_test tests/CellStore64_test.cc
               ${TEST_DEPENDENCIES})
target_link_libraries(CellStore64_test HyperRanger Hypertable)

# AccessGroupGarbageTracker test
add_executable(AccessGroupGarbageTracker_test tests/AccessGroupGarbageTracker_test.cc)
target_link_libraries(AccessGroupGarbageTracker_test HyperRanger Hypertable)

configure_file(${SRC_DIR}/CellStoreScanner_test.golden
               ${DST_DIR}/CellStoreScanner_test.golden)
configure_file(${SRC_DIR}/CellStoreScanner_delete_test.golden
               ${DST_DIR}/CellStoreScanner_delete_test.golden)
configure_file(${SRC_DIR}/CellStore64_test.golden
               ${DST_DIR}/CellStore64_test.golden)

add_custom_command(SOURCE ${HYPERTABLE_SOURCE_DIR}/tests/data/words.gz
    COMMAND gzip ARGS -d < ${HYPERTABLE_SOURCE_DIR}/tests/data/words.gz
                         > ${DST_DIR}/words
    OUTPUT ${DST_DIR}/words)

set(ADDITIONAL_MAKE_CLEAN_FILES ${DST_DIR}/words)

add_test(FileBlockCache FileBlockCache_test)
add_test(QueryCache QueryCache_test)
add_test(TableIdCache TableIdCache_test)
add_test(CellStoreScanner CellStoreScanner_test)
add_test(CellStoreScanner-delete CellStoreScanner_delete_test)
add_test(AG-garbage-tracker AccessGroupGarbageTracker_test)
#add_test(CellStore-64bit CellStore64_test)

if (NOT HT_COMPONENT_INSTALL)
  install(TARGETS HyperRanger Hypertable.RangeServer csdump count_stored
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()
