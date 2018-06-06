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

set(CMAKE_CXX_FLAGS "-DHAVE_NETINET_IN_H ${CMAKE_CXX_FLAGS}")

# IF .thirft instruction change with version it is prefered .thrift by versions
if( NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/thriftgen-${THRIFT_VERSION})
	execute_process(COMMAND  mkdir -p ${CMAKE_CURRENT_SOURCE_DIR}/thriftgen-${THRIFT_VERSION})
endif ()
if( NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/thriftgen-${THRIFT_VERSION}/gen-cpp)
	execute_process(COMMAND  thrift -r --gen cpp 
							 -o ${CMAKE_CURRENT_SOURCE_DIR}/thriftgen-${THRIFT_VERSION}/
							 ${CMAKE_CURRENT_SOURCE_DIR}/Hql.thrift)
	# set current gen-cpp/ included path				 
	execute_process(COMMAND rm -rf ${CMAKE_CURRENT_SOURCE_DIR}/gen-cpp)	
	execute_process(COMMAND cp -r ${CMAKE_CURRENT_SOURCE_DIR}/thriftgen-${THRIFT_VERSION}/gen-cpp
	                              ${CMAKE_CURRENT_SOURCE_DIR}/)			 
endif ()

# libHyperThrift
HT_ADD_LIBS(
	TARGET HyperThrift 
	SRCS ThriftHelper.cc SerializedCellsReader.cc SerializedCellsWriter.cc 
			gen-cpp/Client_constants.cpp 
			gen-cpp/ClientService.cpp 
			gen-cpp/Client_types.cpp
			gen-cpp/Hql_constants.cpp 
			gen-cpp/HqlService.cpp 
			gen-cpp/Hql_types.cpp
	DEPENDENCIES ${THRIFT_LIBRARIES} ${EVENT_LIBRARIES} ${SSL_LIBRARIES})

# libHyperThriftConfig
HT_ADD_LIBS(
	TARGET HyperThriftConfig 
	SRCS Config.cc
	TARGETS HyperThrift Hypertable)

# htThriftBroker
set(ThriftBroker_SRCS ThriftBroker.cc MetricsHandler.cc)
add_executable(htThriftBroker ${ThriftBroker_SRCS})
target_link_libraries(htThriftBroker HyperThriftConfig HyperAppHelper)


### ZLIB TRANSPORT TESTS
# Basic regression test
HT_ADD_TEST(
	NAME ThriftClientZ-cpp
	SRCS tests/client_test.cc
	TARGETS HyperThrift HyperCommon Hypertable 
	ARGS zlib
	PRE_CMD env bash ${INSTALL_DIR}/bin/ht-start-test-servers.sh --clear --opt--thrift-transport=zlib
)
# regression test for SerializedCellsWriter/SerializedCellsReader
HT_ADD_TEST(
	NAME ThriftClientZ-cpp-Serialized
	SRCS tests/client_test.cc
	TARGETS HyperThrift HyperCommon Hypertable 	
	ARGS zlib
	POST_CMD env bash ${INSTALL_DIR}/bin/ht-start-test-servers.sh --clear
)

### FRAMED TRANSPORT TESTS
# Basic regression test
HT_ADD_TEST(
	NAME ThriftClient-cpp
	SRCS tests/client_test.cc
	ARGS framed
	TARGETS HyperThrift HyperCommon Hypertable 
)
# regression test for SerializedCellsWriter/SerializedCellsReader
HT_ADD_TEST(
	NAME ThriftClient-cpp-Serialized
	SRCS tests/serialized_test.cc
	ARGS framed
	TARGETS HyperThrift HyperCommon Hypertable 
)

if (NOT HT_COMPONENT_INSTALL OR PACKAGE_THRIFTBROKER)
  install(TARGETS HyperThrift HyperThriftConfig htThriftBroker
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
  install(FILES Client.h ThriftHelper.h SerializedCellsFlag.h SerializedCellsReader.h SerializedCellsWriter.h Client.thrift Hql.thrift
          DESTINATION include/ThriftBroker)
  install(DIRECTORY gen-cpp DESTINATION include/ThriftBroker)
endif ()

if (PACKAGE_THRIFTBROKER)
  file(GLOB SCRIPTS ${CMAKE_SOURCE_DIR}/bin/*broker.sh ${CMAKE_SOURCE_DIR}/bin/stop-servers.sh ${CMAKE_SOURCE_DIR}/bin/random-wait.sh)
  install(PROGRAMS ${SCRIPTS} DESTINATION bin)
  set(CPACK_PACKAGE_NAME "hypertable-thriftbroker-only")
  include(PackageHelper)
endif ()
