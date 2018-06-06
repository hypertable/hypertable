#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 3 of
# the License.
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

set(Hyperspace_SRCS
ClientKeepaliveHandler.cc
ClientConnectionHandler.cc
Config.cc
DirEntry.cc
DirEntryAttr.cc
HandleCallback.cc
Protocol.cc
Session.cc
HsCommandInterpreter.cc
HsHelpText.cc
HsClientState.cc
)

# lib Hyperspace
HT_ADD_LIBS(
	TARGET Hyperspace 
	SRCS ${Hyperspace_SRCS}
	TARGETS HyperTools
)

set(Master_SRCS
StateDbKeys.cc
BerkeleyDbFilesystem.cc
Event.cc
Master.cc
MetricsHandler.cc
request/RequestHandlerMkdir.cc
request/RequestHandlerDelete.cc
request/RequestHandlerExpireSessions.cc
request/RequestHandlerRenewSession.cc
request/RequestHandlerOpen.cc
request/RequestHandlerClose.cc
request/RequestHandlerAttrSet.cc
request/RequestHandlerAttrGet.cc
request/RequestHandlerAttrIncr.cc
request/RequestHandlerAttrExists.cc
request/RequestHandlerAttrList.cc
request/RequestHandlerAttrDel.cc
request/RequestHandlerExists.cc
request/RequestHandlerReaddir.cc
request/RequestHandlerReaddirAttr.cc
request/RequestHandlerReadpathAttr.cc
request/RequestHandlerLock.cc
request/RequestHandlerRelease.cc
request/RequestHandlerShutdown.cc
request/RequestHandlerStatus.cc
request/RequestHandlerHandshake.cc
request/RequestHandlerDoMaintenance.cc
request/RequestHandlerDestroySession.cc
response/ResponseCallbackOpen.cc
response/ResponseCallbackExists.cc
response/ResponseCallbackAttrGet.cc
response/ResponseCallbackAttrIncr.cc
response/ResponseCallbackAttrExists.cc
response/ResponseCallbackAttrList.cc
response/ResponseCallbackLock.cc
response/ResponseCallbackReaddir.cc
response/ResponseCallbackReaddirAttr.cc
response/ResponseCallbackReadpathAttr.cc
response/ResponseCallbackStatus.cc
ServerConnectionHandler.cc
ServerKeepaliveHandler.cc
main.cc
)


# htHyperspace
add_executable(htHyperspace ${Master_SRCS})
target_link_libraries(htHyperspace Hyperspace ${BDB_LIBRARIES})

# BerkeleyDbFilesystem test
HT_ADD_TEST(
	NAME BerkeleyDbFilesystem
	SRCS tests/bdb_fs_test.cc BerkeleyDbFilesystem.cc StateDbKeys.cc
	TARGETS Hypertable Hyperspace
	EXT_DEPS ${BDB_LIBRARIES}
)
#
# Copy test files
#
set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Hyperspace/tests")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Hyperspace")
configure_file(${SRC_DIR}/bdb_fs_test.golden ${DST_DIR}/bdb_fs_test.golden)


if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)

  install(FILES ${HEADERS} DESTINATION include/Hyperspace)

  install(TARGETS Hyperspace htHyperspace
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()
