#
# Copyright (C) 2007-2012 Hypertable, Inc.
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
ConnectionHandler.cc
Context.cc
GcWorker.cc
DispatchHandlerOperation.cc
DispatchHandlerOperationGetStatistics.cc
LoadBalancer.cc
LoadBalancerBasic.cc
LoadBalancerBasicDistributeTableRanges.cc
LoadBalancerBasicDistributeLoad.cc
LoadBalancerBasicOffloadServers.cc
MetaLogDefinitionMaster.cc
Monitoring.cc
Operation.cc
OperationAlterTable.cc
OperationBalance.cc
OperationCollectGarbage.cc
OperationCreateNamespace.cc
OperationCreateTable.cc
OperationDropNamespace.cc
OperationDropTable.cc
OperationGatherStatistics.cc
OperationInitialize.cc
OperationMoveRange.cc
OperationProcessor.cc
OperationRecoverServer.cc
OperationRegisterServer.cc
OperationRelinquishAcknowledge.cc
OperationRenameTable.cc
OperationStatus.cc
OperationSystemUpgrade.cc
OperationWaitForServers.cc
OperationLoadBalancer.cc
OperationStop.cc
RangeServerConnection.cc
ReferenceManager.cc
ResponseManager.cc
Utility.cc
ServerMetrics.cc
RangeMetrics.cc
RSMetrics.cc
)

# HyperMaster Lib
add_library(HyperMaster ${Master_SRCS})
target_link_libraries(HyperMaster m Hyperspace HyperDfsBroker Hypertable ${RRD_LIBRARIES})

# Hypertable.Master
add_executable(Hypertable.Master main.cc)
target_link_libraries(Hypertable.Master HyperMaster Hyperspace Hypertable HyperDfsBroker ${MALLOC_LIBRARY})

# op_test_driver
add_executable(op_test_driver tests/op_test_driver.cc)
target_link_libraries(op_test_driver HyperMaster Hyperspace Hypertable HyperDfsBroker ${MALLOC_LIBRARY})

# op_dependency_test
add_executable(op_dependency_test tests/op_dependency_test.cc tests/OperationTest.cc)
target_link_libraries(op_dependency_test HyperMaster Hyperspace Hypertable HyperDfsBroker ${MALLOC_LIBRARY})

#
# Copy test files
#
set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Hypertable/Master/tests")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Hypertable/Master")

add_executable(htgc htgc.cc GcWorker.cc)
target_link_libraries(htgc HyperDfsBroker Hypertable ${RRD_LIBRARIES})

add_test(MasterOperation-TestSetup env INSTALL_DIR=${INSTALL_DIR} 
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/op_test_setup.sh)
add_test(MasterOperation-Proccessor op_dependency_test)
add_test(MasterOperation-Initialize op_test_driver initialize)
add_test(MasterOperation-SystemUpgrade op_test_driver system_upgrade)
add_test(MasterOperation-CreateNamespace op_test_driver create_namespace)
add_test(MasterOperation-DropNamespace op_test_driver drop_namespace)
add_test(MasterOperation-CreateTable op_test_driver create_table)
add_test(MasterOperation-CreateTableWithIndex op_test_driver
    create_table_with_index)
add_test(MasterOperation-RenameTable op_test_driver rename_table)
add_test(MasterOperation-MoveRange op_test_driver move_range)


if (NOT HT_COMPONENT_INSTALL)
  install(TARGETS HyperMaster Hypertable.Master htgc op_test_driver op_dependency_test
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()
