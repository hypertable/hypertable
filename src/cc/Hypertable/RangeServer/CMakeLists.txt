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

add_subdirectory(tests)

set(RangeServer_SRCS
AccessGroup.cc
AccessGroupGarbageTracker.cc
AccessGroupHintsFile.cc
CellCache.cc
CellCacheAllocator.cc
CellCacheManager.cc
CellCacheScanner.cc
CellListScannerBuffer.cc
CellStore.cc
CellStoreFactory.cc
CellStoreReleaseCallback.cc
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
CellStoreTrailerV7.cc
CellStoreV0.cc
CellStoreV1.cc
CellStoreV2.cc
CellStoreV3.cc
CellStoreV4.cc
CellStoreV5.cc
CellStoreV6.cc
CellStoreV7.cc
Config.cc
ConnectionHandler.cc
FileBlockCache.cc
FillScanBlock.cc
FragmentData.cc
Global.cc
GroupCommit.cc
GroupCommitTimerHandler.cc
HyperspaceSessionHandler.cc
HyperspaceTableCache.cc
IndexUpdater.cc
KeyCompressorNone.cc
KeyCompressorPrefix.cc
KeyDecompressorNone.cc
KeyDecompressorPrefix.cc
LiveFileTracker.cc
LoadMetricsRange.cc
LocationInitializer.cc
LogReplayBarrier.cc
MaintenancePrioritizer.cc
MaintenancePrioritizerLogCleanup.cc
MaintenancePrioritizerLowMemory.cc
MaintenanceQueue.cc
MaintenanceScheduler.cc
MaintenanceTaskCompaction.cc
MaintenanceTaskDeferredInitialization.cc
MaintenanceTaskMemoryPurge.cc
MaintenanceTaskRelinquish.cc
MaintenanceTaskSplit.cc
MaintenanceTaskWorkQueue.cc
MergeScannerAccessGroup.cc
MergeScannerRange.cc
MetaLogDefinitionRangeServer.cc
MetaLogEntityRange.cc
MetaLogEntityRemoveOkLogs.cc
MetaLogEntityTaskAcknowledgeRelinquish.cc
MetadataNormal.cc
MetadataRoot.cc
PhantomRange.cc
PhantomRangeMap.cc
QueryCache.cc
Range.cc
RangeReplayBuffer.cc
RangeServer.cc
ReplayBuffer.cc
ReplayDispatchHandler.cc
Request/Handler/AcknowledgeLoad.cc
Request/Handler/CommitLogSync.cc
Request/Handler/Compact.cc
Request/Handler/CreateScanner.cc
Request/Handler/DestroyScanner.cc
Request/Handler/DoMaintenance.cc
Request/Handler/DropRange.cc
Request/Handler/DropTable.cc
Request/Handler/Dump.cc
Request/Handler/DumpPseudoTable.cc
Request/Handler/FetchScanblock.cc
Request/Handler/GetStatistics.cc
Request/Handler/GroupCommit.cc
Request/Handler/Heapcheck.cc
Request/Handler/LoadRange.cc
Request/Handler/MetadataSync.cc
Request/Handler/PhantomCommitRanges.cc
Request/Handler/PhantomLoad.cc
Request/Handler/PhantomPrepareRanges.cc
Request/Handler/PhantomUpdate.cc
Request/Handler/RelinquishRange.cc
Request/Handler/ReplayFragments.cc
Request/Handler/SetState.cc
Request/Handler/Shutdown.cc
Request/Handler/Status.cc
Request/Handler/TableMaintenanceDisable.cc
Request/Handler/TableMaintenanceEnable.cc
Request/Handler/Update.cc
Request/Handler/UpdateSchema.cc
Request/Handler/WaitForMaintenance.cc
Response/Callback/AcknowledgeLoad.cc
Response/Callback/CreateScanner.cc
Response/Callback/GetStatistics.cc
Response/Callback/PhantomUpdate.cc
Response/Callback/Status.cc
Response/Callback/Update.cc
ScanContext.cc
ScannerMap.cc
ServerState.cc
TableInfo.cc
TableInfoMap.cc
TimerHandler.cc
UpdatePipeline.cc
)

if (USE_TCMALLOC)
  add_definitions(-DCLEAN_SHUTDOWN)
endif ()

# RangeServer Lib
HT_ADD_LIBS(
	TARGET HyperRanger 
	SRCS ${RangeServer_SRCS}
	TARGETS HyperFsBroker Hypertable
)

# htRangeServer
add_executable(htRangeServer main.cc)
target_link_libraries(htRangeServer HyperRanger)

# ht_csdump
add_executable(ht_csdump csdump.cc)
target_link_libraries(ht_csdump HyperRanger)

# ht_csvalidate
add_executable(ht_csvalidate csvalidate.cc)
target_link_libraries(ht_csvalidate HyperRanger)

# ht_count_stored - program to diff two sorted files
add_executable(ht_count_stored count_stored.cc)
target_link_libraries(ht_count_stored HyperRanger)

if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)
  install(FILES ${HEADERS}
      DESTINATION include/Hypertable/RangeServer)
  file(GLOB HEADERS Request/Handler/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/RangeServer/Request/Handler)
  file(GLOB HEADERS Response/Callback/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/RangeServer/Response/Callback)
  install(TARGETS HyperRanger htRangeServer ht_csdump ht_csvalidate ht_count_stored
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()

