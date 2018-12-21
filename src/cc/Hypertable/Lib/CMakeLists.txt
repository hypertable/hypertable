#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

set(Hypertable_SRCS
AccessGroupSpec.cc
ApacheLogParser.cc
BalancePlan.cc
BlockCompressionCodec.cc
BlockCompressionCodecBmz.cc
BlockCompressionCodecLzo.cc
BlockCompressionCodecNone.cc
BlockCompressionCodecQuicklz.cc
BlockCompressionCodecSnappy.cc
BlockCompressionCodecZlib.cc
BlockCompressionCodecZstd.cc
BlockHeader.cc
BlockHeaderCellStore.cc
BlockHeaderCommitLog.cc
Canonicalize.cc
Cell.cc
CellInterval.cc
Client.cc
ClusterId.cc
ColumnFamilySpec.cc
ColumnPredicate.cc
CommitLog.cc
CommitLogBlockStream.cc
CommitLogReader.cc
CompressorFactory.cc
Config.cc
DataGenerator.cc
DataGeneratorRandom.cc
FixedRandomStringGenerator.cc
Future.cc
HqlCommandInterpreter.cc
HqlHelpText.cc
HqlInterpreter.cc
IndexTables.cc
IntervalScannerAsync.cc
Key.cc
KeySpec.cc
LegacyDecoder.cc
LoadDataEscape.cc
LoadDataSource.cc
LoadDataSourceFactory.cc
LoadDataSourceFileDfs.cc
LoadDataSourceFileLocal.cc
LoadDataSourceStdin.cc
LocationCache.cc
Master/Client.cc
Master/EventHandlerMasterChange.cc
Master/HyperspaceCallback.cc
Master/NamespaceFlag.cc
Master/Request/Parameters/AlterTable.cc
Master/Request/Parameters/Balance.cc
Master/Request/Parameters/Compact.cc
Master/Request/Parameters/CreateNamespace.cc
Master/Request/Parameters/CreateTable.cc
Master/Request/Parameters/DropNamespace.cc
Master/Request/Parameters/DropTable.cc
Master/Request/Parameters/FetchResult.cc
Master/Request/Parameters/MoveRange.cc
Master/Request/Parameters/PhantomCommitComplete.cc
Master/Request/Parameters/PhantomPrepareComplete.cc
Master/Request/Parameters/RecreateIndexTables.cc
Master/Request/Parameters/RegisterServer.cc
Master/Request/Parameters/RelinquishAcknowledge.cc
Master/Request/Parameters/RenameTable.cc
Master/Request/Parameters/ReplayComplete.cc
Master/Request/Parameters/ReplayStatus.cc
Master/Request/Parameters/SetState.cc
Master/Request/Parameters/Stop.cc
Master/Response/Parameters/RegisterServer.cc
Master/Response/Parameters/Status.cc
Master/Response/Parameters/SystemStatus.cc
MetaLog.cc
MetaLogEntity.cc
MetaLogEntityHeader.cc
MetaLogReader.cc
MetaLogWriter.cc
NameIdMapper.cc
Namespace.cc
NamespaceCache.cc
ProfileDataScanner.cc
PseudoTables.cc
QualifiedRangeSpec.cc
RS_METRICS/RangeMetrics.cc
RS_METRICS/ReaderTable.cc
RS_METRICS/ServerMetrics.cc
RangeLocator.cc
RangeMoveSpec.cc
RangeServer/Client.cc
RangeServer/Protocol.cc
RangeServer/Request/Parameters/AcknowledgeLoad.cc
RangeServer/Request/Parameters/CommitLogSync.cc
RangeServer/Request/Parameters/Compact.cc
RangeServer/Request/Parameters/CreateScanner.cc
RangeServer/Request/Parameters/DestroyScanner.cc
RangeServer/Request/Parameters/DropRange.cc
RangeServer/Request/Parameters/DropTable.cc
RangeServer/Request/Parameters/Dump.cc
RangeServer/Request/Parameters/DumpPseudoTable.cc
RangeServer/Request/Parameters/FetchScanblock.cc
RangeServer/Request/Parameters/GetStatistics.cc
RangeServer/Request/Parameters/Heapcheck.cc
RangeServer/Request/Parameters/LoadRange.cc
RangeServer/Request/Parameters/PhantomCommitRanges.cc
RangeServer/Request/Parameters/PhantomLoad.cc
RangeServer/Request/Parameters/PhantomPrepareRanges.cc
RangeServer/Request/Parameters/PhantomUpdate.cc
RangeServer/Request/Parameters/RelinquishRange.cc
RangeServer/Request/Parameters/ReplayFragments.cc
RangeServer/Request/Parameters/SetState.cc
RangeServer/Request/Parameters/TableMaintenanceDisable.cc
RangeServer/Request/Parameters/TableMaintenanceEnable.cc
RangeServer/Request/Parameters/Update.cc
RangeServer/Request/Parameters/UpdateSchema.cc
RangeServer/Response/Parameters/AcknowledgeLoad.cc
RangeServer/Response/Parameters/CreateScanner.cc
RangeServer/Response/Parameters/GetStatistics.cc
RangeServer/Response/Parameters/Status.cc
RangeServerRecovery/FragmentReplayPlan.cc
RangeServerRecovery/Plan.cc
RangeServerRecovery/ReceiverPlan.cc
RangeServerRecovery/ReplayPlan.cc
RangeServerRecovery/ServerReceiverPlan.cc
RangeSpec.cc
RangeState.cc
Result.cc
RootFileHandler.cc
RowInterval.cc
ScanBlock.cc
ScanCells.cc
ScanSpec.cc
Schema.cc
StatsRangeServer.cc
StatsTable.cc
SystemVariable.cc
Table.cc
TableCache.cc
TableCallback.cc
TableDumper.cc
TableIdentifier.cc
TableMutator.cc
TableMutatorAsync.cc
TableMutatorAsyncDispatchHandler.cc
TableMutatorAsyncHandler.cc
TableMutatorAsyncScatterBuffer.cc
TableMutatorFlushHandler.cc
TableMutatorIntervalHandler.cc
TableMutatorShared.cc
TableMutatorSyncDispatchHandler.cc
TableParts.cc
TableScanner.cc
TableScannerAsync.cc
TableScannerDispatchHandler.cc
TableScannerHandler.cc
TableSplit.cc
TestSource.cc
bmz/bmz.c
)

# lib Hypertable
HT_ADD_LIBS(
	TARGET Hypertable 
	SRCS ${Hypertable_SRCS}
	TARGETS Hyperspace HyperFsBroker
	DEPENDENCIES ${SNAPPY_LIBRARIES} ${ZSTD_LIBRARIES} ${EXPAT_LIBRARIES})
add_dependencies(Hypertable HyperComm Hyperspace HyperCommon)

# test files
set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Hypertable/Lib/tests")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Hypertable/Lib")

configure_file(${HYPERTABLE_SOURCE_DIR}/tests/data/random.dat
               ${DST_DIR}/random.dat COPYONLY)
configure_file(${HYPERTABLE_SOURCE_DIR}/conf/hypertable.cfg
               ${DST_DIR}/hypertable.cfg)
			   
add_test(Hypertable-Lib-TestSetup env INSTALL_DIR=${INSTALL_DIR}
         ${SRC_DIR}/test_setup.sh)
		 
# ColumnFamilySpec_test
HT_ADD_TEST(
	NAME ColumnFamilySpec
	SRCS tests/ColumnFamilySpec_test.cc
	TARGETS Hypertable
)
configure_file(${SRC_DIR}/ColumnFamilySpec_test.golden ${DST_DIR}/ColumnFamilySpec_test.golden)

# AccessGroupSpec_test
HT_ADD_TEST(
	NAME AccessGroupSpec
	SRCS tests/AccessGroupSpec_test.cc
	TARGETS Hypertable
)
configure_file(${SRC_DIR}/AccessGroupSpec_test.golden ${DST_DIR}/AccessGroupSpec_test.golden)

# Schema_test
HT_ADD_TEST(
	NAME Schema
	SRCS tests/Schema_test.cc
	TARGETS Hypertable
)
configure_file(${SRC_DIR}/test-schemas.xml ${DST_DIR}/test-schemas.xml COPYONLY)
configure_file(${SRC_DIR}/Schema_test.golden ${DST_DIR}/Schema_test.golden)

# locationCacheTest
HT_ADD_TEST(
	NAME LocationCache
	SRCS tests/locationCacheTest.cc
	TARGETS Hypertable
)
configure_file(${SRC_DIR}/locationCacheTest.golden ${DST_DIR}/locationCacheTest.golden)

# loadDataSourceTest
HT_ADD_TEST(
	NAME LoadDataSource
	SRCS tests/loadDataSourceTest.cc
	TARGETS Hypertable
)
configure_file(${SRC_DIR}/loadDataSourceTest.golden
               ${DST_DIR}/loadDataSourceTest.golden)
configure_file(${SRC_DIR}/loadDataSourceTest.dat
               ${DST_DIR}/loadDataSourceTest.dat)
configure_file(${SRC_DIR}/loadDataSourceTest-header.golden
               ${DST_DIR}/loadDataSourceTest-header.golden)
configure_file(${SRC_DIR}/loadDataSourceTest-header.dat
               ${DST_DIR}/loadDataSourceTest-header.dat)
configure_file(${SRC_DIR}/loadDataSourceTest-qualified-header.golden
               ${DST_DIR}/loadDataSourceTest-qualified-header.golden)
configure_file(${SRC_DIR}/loadDataSourceTest-qualified-header.dat
               ${DST_DIR}/loadDataSourceTest-qualified-header.dat)

# compressor_test

set(EXT_DEPS "")
if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
   set(EXT_DEPS "m")
endif (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
# bmz binaries
add_executable(bmz-test bmz/bmz-test.c)
target_link_libraries(bmz-test Hypertable ${EXT_DEPS})
add_executable(bmzip bmz/bmzip.c)
target_link_libraries(bmzip Hypertable ${EXT_DEPS})
set(EXT_DEPS '')

HT_ADD_TEST(
	NAME BlockCompressor
	SRCS tests/compressor_test.cc
	TARGETS Hypertable
	EXEC_OPTS bmz lzo none quicklz zlib snappy zstd
)

# block_header_test
HT_ADD_TEST(
	NAME BlockHeader
	SRCS tests/block_header_test.cc
	TARGETS Hypertable
)

# commit_log_test
HT_ADD_TEST(
	NAME CommitLog
	SRCS tests/commit_log_test.cc
	TARGETS Hypertable
)

# escape_test
HT_ADD_TEST(
	NAME LoadDataEscape
	SRCS tests/escape_test.cc
	TARGETS Hypertable
)

# large_insert_test
HT_ADD_TEST(
	NAME Client-large-block
	SRCS tests/large_insert_test.cc
	TARGETS Hypertable
)

# async_api_test 
HT_ADD_TEST(
	NAME Client-async-api
	SRCS tests/async_api_test.cc
	TARGETS Hypertable
)
configure_file(${SRC_DIR}/asyncApiTest.golden ${DST_DIR}/asyncApiTest.golden)

# scanner_abrupt_end_test
add_executable(scanner_abrupt_end_test tests/scanner_abrupt_end_test.cc)
target_link_libraries(scanner_abrupt_end_test Hypertable)#HT_ADD_TEST(
#	NAME Client-scanner_abrupt_end
#	SRCS tests/scanner_abrupt_end_test.cc
#	TARGETS Hypertable
#)

# future_abrupt_end_test
add_executable(future_abrupt_end_test tests/future_abrupt_end_test.cc)
target_link_libraries(future_abrupt_end_test Hypertable)
#HT_ADD_TEST(
#	NAME Client-future_abrupt_end
#	SRCS tests/future_abrupt_end_test.cc
#	TARGETS Hypertable
#)

# future_mutator_cancel_test
add_executable(future_mutator_cancel_test tests/future_mutator_cancel_test.cc)
target_link_libraries(future_mutator_cancel_test Hypertable)
#HT_ADD_TEST(
#	NAME Client-future_mutator_cancel
#	SRCS tests/future_mutator_cancel_test.cc
#	TARGETS Hypertable
#)

# future_test 
HT_ADD_TEST(
	NAME Client-future
	SRCS tests/future_test.cc
	TARGETS Hypertable
)
configure_file(${SRC_DIR}/future_test.cfg ${DST_DIR}/future_test.cfg)

# key_spec_test 
HT_ADD_TEST(
	NAME KeySpec
	SRCS tests/key_spec_test.cc
	TARGETS Hypertable
	ENV "INSTALL_DIR=${INSTALL_DIR} ${CMAKE_CURRENT_BINARY_DIR}/"
)

# scan_spec_test
HT_ADD_TEST(
	NAME ScanSpec-basic-tests
	SRCS tests/scan_spec_test.cc
	TARGETS Hypertable
)

# indices_test
HT_ADD_TEST(
	NAME Secondary-Indices-tests
	SRCS tests/indices_test.cc
	TARGETS Hypertable
)
configure_file(${SRC_DIR}/indices_test.golden ${DST_DIR}/indices_test.golden)

# row_delete_test
HT_ADD_TEST(
	NAME Client-row-delete
	SRCS tests/row_delete_test.cc
	TARGETS Hypertable
)

# MutatorNoLogSyncTest
add_executable(MutatorNoLogSyncTest tests/MutatorNoLogSyncTest.cc)
target_link_libraries(MutatorNoLogSyncTest Hypertable)
#HT_ADD_TEST(
#	NAME MutatorNoLogSyncTest
#	SRCS tests/MutatorNoLogSyncTest.cc
#	TARGETS Hypertable
#)
configure_file (${SRC_DIR}/MutatorNoLogSyncTest.cfg ${DST_DIR}/MutatorNoLogSyncTest.cfg)

# periodic_flush_test
HT_ADD_TEST(
	NAME Client-periodic-flush
	SRCS tests/periodic_flush_test.cc
	TARGETS Hypertable
)

# name_id_mapper_test 
HT_ADD_TEST(
	NAME NameIdMapper
	SRCS tests/name_id_mapper_test.cc
	TARGETS Hypertable Hyperspace
	ARGS "--config=${DST_DIR}/name_id_mapper_test.cfg"
)
configure_file(${SRC_DIR}/name_id_mapper_test.cfg ${DST_DIR}/name_id_mapper_test.cfg)

# rangeserver_serialize_test 
HT_ADD_TEST(
	NAME StatsRangeServer-serialize
	SRCS tests/rangeserver_serialize_test.cc
	TARGETS Hypertable Hyperspace
)

# MetaLog test
HT_ADD_TEST(
	NAME MetaLog
	SRCS tests/metalog_test.cc
	TARGETS Hypertable Hyperspace
)
configure_file(${SRC_DIR}/metalog_test.golden ${DST_DIR}/metalog_test.golden)
configure_file(${SRC_DIR}/metalog_test2.golden ${DST_DIR}/metalog_test2.golden)
configure_file(${SRC_DIR}/metalog_test3.golden ${DST_DIR}/metalog_test3.golden)





if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)
  install(FILES ${HEADERS}
        DESTINATION include/Hypertable/Lib)
  file(GLOB HEADERS RangeServerRecovery/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/Lib/RangeServerRecovery)
  file(GLOB HEADERS RangeServer/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/Lib/RangeServer)
  file(GLOB HEADERS RangeServer/Request/Parameters/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/Lib/RangeServer/Request/Parameters)
  file(GLOB HEADERS RangeServer/Response/Parameters/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/Lib/RangeServer/Response/Parameters)

  file(GLOB HEADERS Master/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/Lib/Master)
  file(GLOB HEADERS Master/Request/Parameters/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/Lib/Master/Request/Parameters)
  file(GLOB HEADERS Master/Response/Parameters/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/Hypertable/Lib/Master/Response/Parameters)
  install(TARGETS Hypertable
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib)
endif ()
