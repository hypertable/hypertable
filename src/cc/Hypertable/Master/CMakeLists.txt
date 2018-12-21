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

set(Master_SRCS
BalanceAlgorithmEvenRanges.cc
BalanceAlgorithmLoad.cc
BalanceAlgorithmOffload.cc
BalancePlanAuthority.cc
ConnectionHandler.cc
Context.cc
GcWorker.cc
DispatchHandlerOperation.cc
DispatchHandlerOperationGetStatistics.cc
DispatchHandlerOperationSetState.cc
DispatchHandlerOperationSystemStatus.cc
HyperspaceMasterFile.cc
LoadBalancer.cc
MetaLogDefinitionMaster.cc
MetricsHandler.cc
Monitoring.cc
Operation.cc
OperationAlterTable.cc
OperationBalance.cc
OperationCollectGarbage.cc
OperationCompact.cc
OperationRecoveryBlocker.cc
OperationCreateNamespace.cc
OperationCreateTable.cc
OperationDropNamespace.cc
OperationDropTable.cc
OperationGatherStatistics.cc
OperationInitialize.cc
OperationMoveRange.cc
OperationProcessor.cc
OperationRecover.cc
OperationRecoverRanges.cc
OperationRecreateIndexTables.cc
OperationRegisterServer.cc
OperationRegisterServerBlocker.cc
OperationRelinquishAcknowledge.cc
OperationRenameTable.cc
OperationSetState.cc
OperationStatus.cc
OperationSystemStatus.cc
OperationSystemUpgrade.cc
OperationTimedBarrier.cc
OperationToggleTableMaintenance.cc
OperationWaitForServers.cc
OperationStop.cc
RangeServerConnection.cc
RangeServerConnectionManager.cc
ResponseManager.cc
Utility.cc
RecoveredServers.cc
SystemState.cc
)

# HyperMaster Lib
HT_ADD_LIBS(
	TARGET HyperMaster 
	SRCS ${Master_SRCS}
	TARGETS Hyperspace HyperFsBroker Hypertable
)
	
# htMaster
add_executable(htMaster main.cc)
target_link_libraries(htMaster HyperMaster HyperRanger)

# ht_gc
add_executable(ht_gc gc.cc GcWorker.cc)
target_link_libraries(ht_gc HyperMaster HyperFsBroker HyperRanger Hypertable ${RRD_LIBRARIES})


#
# Copy test files
#
set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Hypertable/Master/tests")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Hypertable/Master")

		 
# op_test_driver
HT_ADD_TEST(
	NAME MasterOperation
	SRCS tests/op_test_driver.cc
	TARGETS HyperMaster HyperRanger
	EXEC_OPTS initialize 
			  system_upgrade 
			  create_namespace 
			  drop_namespace 
			  create_table 
			  create_table_with_index 
			  drop_table
			  rename_table
			  move_range
			  balance_plan_authority
			  toggle_table_maintenance
			  recreate_index_tables
	PRE_CMD_TYPE env INSTALL_DIR=${INSTALL_DIR} bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/op_test_setup.sh
)
# drop_namespace_if_exists
configure_file(${SRC_DIR}/master_initialize.golden 
			   ${DST_DIR}/master_initialize.golden COPYONLY)
configure_file(${SRC_DIR}/create_namespace.golden
			   ${DST_DIR}/create_namespace.golden COPYONLY)
configure_file(${SRC_DIR}/drop_namespace.golden  
			   ${DST_DIR}/drop_namespace.golden COPYONLY)
configure_file(${SRC_DIR}/create_table.golden 
			   ${DST_DIR}/create_table.golden COPYONLY)
configure_file(${SRC_DIR}/create_table_with_index.golden 
			   ${DST_DIR}/create_table_with_index.golden COPYONLY)
configure_file(${SRC_DIR}/drop_table.golden 
			   ${DST_DIR}/drop_table.golden COPYONLY)
configure_file(${SRC_DIR}/rename_table.golden
               ${DST_DIR}/rename_table.golden COPYONLY)
configure_file(${SRC_DIR}/move_range.golden  
			   ${DST_DIR}/move_range.golden COPYONLY)
configure_file(${SRC_DIR}/balance_plan_authority_test.golden
			   ${DST_DIR}/balance_plan_authority_test.golden COPYONLY)
configure_file(${SRC_DIR}/toggle_table_maintenance.golden
               ${DST_DIR}/toggle_table_maintenance.golden COPYONLY)
configure_file(${SRC_DIR}/recreate_index_tables.golden
               ${DST_DIR}/recreate_index_tables.golden COPYONLY)


# op_dependency_test
HT_ADD_TEST(
	NAME MasterOperation-Processor
	SRCS tests/op_dependency_test.cc tests/OperationTest.cc
	TARGETS Hypertable HyperMaster HyperRanger
)

# system_state_test
HT_ADD_TEST(
	NAME SystemState
	SRCS tests/system_state_test.cc
	TARGETS Hypertable HyperMaster HyperRanger
)
configure_file(${SRC_DIR}/system_state_test.golden
			   ${DST_DIR}/system_state_test.golden COPYONLY)



if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)
  install(FILES ${HEADERS}
      DESTINATION include/Hypertable/Master)
  install(TARGETS HyperMaster htMaster ht_gc
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()
