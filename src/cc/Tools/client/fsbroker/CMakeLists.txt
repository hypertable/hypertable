#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 3 of
# the License.
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


set(fsbroker_SRCS
CommandInterpreter.cc
)

# ht_fsbroker - command interpreter
add_executable(ht_fsbroker fsbroker.cc ${fsbroker_SRCS})
target_link_libraries(ht_fsbroker HyperCommon HyperComm HyperFsBroker Hypertable ${EDITLINE_LIBRARIES})



set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Tools/client/fsbroker")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Tools/client/fsbroker")

add_custom_command(SOURCE ${HYPERTABLE_SOURCE_DIR}/tests/data/words.gz
    COMMAND gzip ARGS -d < ${HYPERTABLE_SOURCE_DIR}/tests/data/words.gz
                         > ${DST_DIR}/words
    OUTPUT ${DST_DIR}/words)

SET_DIRECTORY_PROPERTIES(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${DST_DIR}/words") 

# fsTest
HT_ADD_TEST(
	NAME HyperFsBroker
	SRCS fsTest.cc FsTestThreadFunction.cc
	TARGETS Hypertable HyperCommon HyperComm HyperFsBroker
	EXEC_DEPS ${DST_DIR}/words
)
configure_file(${SRC_DIR}/fsTest.golden ${DST_DIR}/fsTest.golden COPYONLY)


if (NOT HT_COMPONENT_INSTALL)
  install(TARGETS ht_fsbroker
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)
endif ()
