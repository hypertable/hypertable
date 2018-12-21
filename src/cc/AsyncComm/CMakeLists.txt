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

set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/AsyncComm/tests")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/AsyncComm")

set(TEST_DEPENDENCIES ${DST_DIR}/words)

set(AsyncComm_SRCS
DispatchHandlerSynchronizer.cc
Comm.cc
CommAddress.cc
CommHeader.cc
ConfigHandler.cc
Config.cc
ConnectionManager.cc
Event.cc
HandlerMap.cc
IOHandler.cc
IOHandlerAccept.cc
IOHandlerData.cc
IOHandlerDatagram.cc
IOHandlerRaw.cc
PollEvent.cc
Protocol.cc
ProxyMap.cc
Reactor.cc
ReactorFactory.cc
ReactorRunner.cc
RequestCache.cc
ResponseCallback.cc
)

if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
  link_libraries(nsl socket resolv)
endif (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")

# lib HyperComm
HT_ADD_LIBS(
	TARGET HyperComm 
	SRCS ${AsyncComm_SRCS}
	TARGETS HyperCommon)

# sampleClient
add_executable(sampleClient sampleClient.cc)
target_link_libraries(sampleClient HyperComm)

# testServer
add_executable(testServer testServer.cc)
target_link_libraries(testServer HyperComm)

# commTest
HT_ADD_TEST(
	NAME HyperComm
	SRCS tests/commTest.cc tests/CommTestThreadFunction.cc ${TEST_DEPENDENCIES}
	TARGETS Hypertable 
)

# commTestDatagram
HT_ADD_TEST(
	NAME HyperComm-datagram
	SRCS tests/commTestDatagram.cc tests/CommTestDatagramThreadFunction.cc ${TEST_DEPENDENCIES}
	TARGETS Hypertable
)

# commTestTimeout
HT_ADD_TEST(
	NAME HyperComm-timeout
	SRCS tests/commTestTimeout.cc
	TARGETS Hypertable 
)
configure_file(${SRC_DIR}/commTestTimeout.golden  ${DST_DIR}/commTestTimeout.golden)


# commTestTimer
HT_ADD_TEST(
	NAME HyperComm-timer
	SRCS tests/commTestTimer.cc
	TARGETS Hypertable 
)
configure_file(${SRC_DIR}/commTestTimer.golden ${DST_DIR}/commTestTimer.golden)

# commTestReverseRequest
HT_ADD_TEST(
	NAME HyperComm-reverse-request
	SRCS tests/commTestReverseRequest.cc
	TARGETS Hypertable 
)
configure_file(${SRC_DIR}/commTestReverseRequest.golden ${DST_DIR}/commTestReverseRequest.golden)



configure_file(${SRC_DIR}/datafile.txt ${DST_DIR}/datafile.txt)

add_custom_command(SOURCE ${HYPERTABLE_SOURCE_DIR}/tests/data/words.gz
    COMMAND gzip ARGS -d < ${HYPERTABLE_SOURCE_DIR}/tests/data/words.gz
                         > ${DST_DIR}/words
    OUTPUT ${DST_DIR}/words)
set(ADDITIONAL_MAKE_CLEAN_FILES ${DST_DIR}/words)


if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)

  install(FILES ${HEADERS} DESTINATION include/AsyncComm)

  install(TARGETS HyperComm
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
		  ARCHIVE DESTINATION lib)
endif ()
