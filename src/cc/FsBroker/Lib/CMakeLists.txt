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

set(FsBroker_SRCS
Client.cc
ClientBufferedReaderHandler.cc
Config.cc
ConnectionHandler.cc
FileDevice.cc
MetricsHandler.cc
Request/Handler/Append.cc
Request/Handler/Close.cc
Request/Handler/Create.cc
Request/Handler/Debug.cc
Request/Handler/Exists.cc
Request/Handler/Factory.cc
Request/Handler/Flush.cc
Request/Handler/Length.cc
Request/Handler/Mkdirs.cc
Request/Handler/Open.cc
Request/Handler/Pread.cc
Request/Handler/Read.cc
Request/Handler/Readdir.cc
Request/Handler/Remove.cc
Request/Handler/Rename.cc
Request/Handler/Rmdir.cc
Request/Handler/Seek.cc
Request/Handler/Status.cc
Request/Handler/Sync.cc
Request/Parameters/Append.cc
Request/Parameters/Close.cc
Request/Parameters/Create.cc
Request/Parameters/Debug.cc
Request/Parameters/Exists.cc
Request/Parameters/Flush.cc
Request/Parameters/Length.cc
Request/Parameters/Mkdirs.cc
Request/Parameters/Open.cc
Request/Parameters/Pread.cc
Request/Parameters/Read.cc
Request/Parameters/Readdir.cc
Request/Parameters/Remove.cc
Request/Parameters/Rename.cc
Request/Parameters/Rmdir.cc
Request/Parameters/Seek.cc
Request/Parameters/Shutdown.cc
Request/Parameters/Sync.cc
Response/Callback/Open.cc
Response/Callback/Read.cc
Response/Callback/Append.cc
Response/Callback/Length.cc
Response/Callback/Readdir.cc
Response/Callback/Status.cc
Response/Callback/Exists.cc
Response/Parameters/Append.cc
Response/Parameters/Exists.cc
Response/Parameters/Length.cc
Response/Parameters/Open.cc
Response/Parameters/Read.cc
Response/Parameters/Readdir.cc
Response/Parameters/Status.cc
StatusManager.cc
Utility.cc
)

# lib HyperFsBroker
HT_ADD_LIBS(
	TARGET HyperFsBroker 
	SRCS ${FsBroker_SRCS}
	TARGETS HyperCommon HyperComm)
add_dependencies(HyperFsBroker HyperCommon HyperComm)

if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)
  install(FILES ${HEADERS} DESTINATION include/FsBroker/Lib)
  file(GLOB HEADERS Request/Handler/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/FsBroker/Lib/Request/Handler)
  file(GLOB HEADERS Request/Parameters/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/FsBroker/Lib/Request/Parameters)
  file(GLOB HEADERS Response/Callback/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/FsBroker/Lib/Response/Callback)
  file(GLOB HEADERS Response/Parameters/*.h)
  install(FILES ${HEADERS}
          DESTINATION include/FsBroker/Lib/Response/Parameters)
  install(TARGETS HyperFsBroker
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()
