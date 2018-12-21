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

set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/HyperAppHelper")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/HyperAppHelper")

set(Helper_SRCS
Unique.cc
Unique.h
Error.h
)
# lib HyperAppHelper
HT_ADD_LIBS(
	TARGET HyperAppHelper 
	SRCS ${Helper_SRCS}
	TARGETS Hypertable HyperCommon)

# tests
HT_ADD_TEST(
	NAME HyperAppHelper-Unique
	SRCS tests/unique_test.cc
	TARGETS HyperAppHelper Hypertable HyperComm HyperCommon
)
HT_ADD_TEST(
	NAME HyperAppHelper-Error
	SRCS tests/error_test.cc
	TARGETS HyperAppHelper HyperAppHelper HyperCommon
)

configure_file(${HYPERTABLE_SOURCE_DIR}/conf/hypertable.cfg
               ${DST_DIR}/hypertable.cfg)

if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)

  install(FILES ${HEADERS} ${VERSION_H} DESTINATION include/HyperAppHelper)

  install(TARGETS HyperAppHelper
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()
