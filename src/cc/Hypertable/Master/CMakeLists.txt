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
add_library(HyperMaster ${Master_SRCS})
target_link_libraries(HyperMaster m Hyperspace HyperFsBroker Hypertable ${RRD_LIBRARIES})

# htMaster
add_executable(htMaster main.cc)
target_link_libraries(htMaster HyperMaster Hyperspace Hypertable HyperRanger HyperFsBroker ${MALLOC_LIBRARY})

# op_test_driver
add_executable(op_test_driver tests/op_test_driver.cc)
target_link_libraries(op_test_driver HyperMaster HyperRanger Hyperspace Hypertable HyperFsBroker ${MALLOC_LIBRARY})

# op_dependency_test
add_executable(op_dependency_test tests/op_dependency_test.cc tests/OperationTest.cc)
target_link_libraries(op_dependency_test HyperMaster HyperRanger Hyperspace Hypertable HyperFsBroker ${MALLOC_LIBRARY})

# system_state_test
add_executable(system_state_test tests/system_state_test.cc)
target_link_libraries(system_state_test HyperCommon HyperMaster HyperRanger Hypertable ${MALLOC_LIBRARY})

#
# Copy test files
#
set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Hypertable/Master/tests")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Hypertable/Master")

add_executable(ht_gc gc.cc GcWorker.cc)
target_link_libraries(ht_gc HyperMaster HyperFsBroker HyperRanger Hypertable ${RRD_LIBRARIES})

configure_file(${SRC_DIR}/balance_plan_authority_test.golden
    ${DST_DIR}/balance_plan_authority_test.golden COPYONLY)

configure_file(${SRC_DIR}/system_state_test.golden
    ${DST_DIR}/system_state_test.golden COPYONLY)

add_test(MasterOperation-TestSetup env INSTALL_DIR=${INSTALL_DIR} 
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/op_test_setup.sh)

add_test(MasterOperation-Processor op_dependency_test)

configure_file(${SRC_DIR}/master_initialize.golden
               ${DST_DIR}/master_initialize.golden COPYONLY)
add_test(MasterOperation-Initialize op_test_driver initialize)

add_test(MasterOperation-SystemUpgrade op_test_driver system_upgrade)

configure_file(${SRC_DIR}/create_namespace.golden
               ${DST_DIR}/create_namespace.golden COPYONLY)
add_test(MasterOperation-CreateNamespace op_test_driver create_namespace)

configure_file(${SRC_DIR}/drop_namespace.golden
               ${DST_DIR}/drop_namespace.golden COPYONLY)
add_test(MasterOperation-DropNamespace op_test_driver drop_namespace)

configure_file(${SRC_DIR}/create_table.golden
               ${DST_DIR}/create_table.golden COPYONLY)
add_test(MasterOperation-CreateTable op_test_driver create_table)

configure_file(${SRC_DIR}/create_table_with_index.golden
               ${DST_DIR}/create_table_with_index.golden COPYONLY)
add_test(MasterOperation-CreateTableWithIndex op_test_driver
         create_table_with_index)

configure_file(${SRC_DIR}/drop_table.golden
               ${DST_DIR}/drop_table.golden COPYONLY)
add_test(MasterOperation-DropTable op_test_driver drop_table)

configure_file(${SRC_DIR}/rename_table.golden
               ${DST_DIR}/rename_table.golden COPYONLY)
add_test(MasterOperation-RenameTable op_test_driver rename_table)

configure_file(${SRC_DIR}/move_range.golden
               ${DST_DIR}/move_range.golden COPYONLY)
add_test(MasterOperation-MoveRange op_test_driver move_range)

add_test(MasterOperation-BalancePlanAuthority op_test_driver balance_plan_authority)

configure_file(${SRC_DIR}/toggle_table_maintenance.golden
               ${DST_DIR}/toggle_table_maintenance.golden COPYONLY)
add_test(MasterOperation-ToggleTableMaintenance op_test_driver toggle_table_maintenance)

configure_file(${SRC_DIR}/recreate_index_tables.golden
               ${DST_DIR}/recreate_index_tables.golden COPYONLY)
add_test(MasterOperation-RecreateIndexTables op_test_driver recreate_index_tables)

add_test(SystemState system_state_test)

if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)
  install(FILES ${HEADERS}
      DESTINATION include/Hypertable/Master)
  install(TARGETS HyperMaster htMaster ht_gc
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()
