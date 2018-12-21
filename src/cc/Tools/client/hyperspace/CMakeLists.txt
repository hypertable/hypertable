#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
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

set(hyperspace_SRCS
hyperspace.cc
)

# ht_hyperspace - Hyperspace client program
add_executable(ht_hyperspace ${hyperspace_SRCS})
target_link_libraries(ht_hyperspace Hyperspace Hypertable)


set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Tools/client/hyperspace")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Tools/client/hyperspace")

# hyperspaceTest
HT_ADD_TEST(
	NAME Hyperspace
	SRCS test/hyperspaceTest.cc
	TARGETS Hypertable HyperComm
)
configure_file(${SRC_DIR}/test/hyperspaceTest.cfg ${DST_DIR}/hyperspaceTest.cfg)
configure_file(${SRC_DIR}/test/client1.golden ${DST_DIR}/client1.golden
               COPYONLY)
configure_file(${SRC_DIR}/test/client2.golden ${DST_DIR}/client2.golden
               COPYONLY)
configure_file(${SRC_DIR}/test/client3.golden ${DST_DIR}/client3.golden
               COPYONLY)

			   
if (NOT HT_COMPONENT_INSTALL)
  install(TARGETS ht_hyperspace RUNTIME DESTINATION bin)
endif ()
