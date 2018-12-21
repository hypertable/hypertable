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

set(hypertable_SRCS
hypertable.cc
)

# ht_hypertable - command interpreter
add_executable(ht_hypertable ${hypertable_SRCS})
target_link_libraries(ht_hypertable Hypertable)


set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Tools/client/hypertable/test")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Tools/client/hypertable")

configure_file(${HYPERTABLE_SOURCE_DIR}/conf/hypertable.cfg
               ${DST_DIR}/hypertable.cfg)


# hypertable_test
HT_ADD_TEST(
	NAME Hypertable-shell
	SRCS test/hypertable_test.cc
	TARGETS Hypertable HyperComm
	ENV "INSTALL_DIR=${INSTALL_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/test/hypertable_test.sh "
)
configure_file(${SRC_DIR}/hypertable_test.golden
               ${DST_DIR}/hypertable_test.golden)
configure_file(${SRC_DIR}/hypertable_test.hql
               ${DST_DIR}/hypertable_test.hql)
configure_file(${SRC_DIR}/hypertable_test.tsv
               ${DST_DIR}/hypertable_test.tsv)
configure_file(${SRC_DIR}/hypertable_test.tsv.gz 
               ${DST_DIR}/hypertable_test.tsv.gz COPYONLY)
configure_file(${SRC_DIR}/indices_test.golden
               ${DST_DIR}/indices_test.golden)
configure_file(${SRC_DIR}/indices_test.hql
               ${DST_DIR}/indices_test.hql)
configure_file(${SRC_DIR}/indices_test_products.tsv
               ${DST_DIR}/indices_test_products.tsv COPYONLY)
configure_file(${SRC_DIR}/hypertable_unknown_cf.tsv
               ${DST_DIR}/hypertable_unknown_cf.tsv)
configure_file(${SRC_DIR}/hypertable_ts.tsv
               ${DST_DIR}/hypertable_ts.tsv)
configure_file(${SRC_DIR}/foobar.tsv
               ${DST_DIR}/foobar.tsv)
configure_file(${SRC_DIR}/single_cell_test.tsv
               ${DST_DIR}/single_cell_test.tsv)
configure_file(${SRC_DIR}/hypertable_select_gz_test.golden
               ${DST_DIR}/hypertable_select_gz_test.golden)
configure_file(${SRC_DIR}/offset_test.golden
               ${DST_DIR}/offset_test.golden)
configure_file(${SRC_DIR}/offset_test.hql
               ${DST_DIR}/offset_test.hql)
configure_file(${SRC_DIR}/timeorder_test.golden
               ${DST_DIR}/timeorder_test.golden)
configure_file(${SRC_DIR}/timeorder_test.hql
               ${DST_DIR}/timeorder_test.hql)


# hypertable_ldi_select_test
HT_ADD_TEST(
	NAME Hypertable-shell-ldi-select 
	SRCS test/hypertable_ldi_select_test.cc
	TARGETS Hypertable HyperFsBroker HyperComm
	ENV "INSTALL_DIR=${INSTALL_DIR} "
	POST_CMD env bash ${INSTALL_DIR}/bin/ht-start-test-servers.sh --clear
)
configure_file(${SRC_DIR}/hypertable_ldi_select_test.golden
               ${DST_DIR}/hypertable_ldi_select_test.golden)
configure_file(${SRC_DIR}/hypertable_ldi_stdin_test_load.hql
               ${DST_DIR}/hypertable_ldi_stdin_test_load.hql)
configure_file(${SRC_DIR}/hypertable_ldi_stdin_test_select.hql
               ${DST_DIR}/hypertable_ldi_stdin_test_select.hql)
configure_file(${SRC_DIR}/hypertable_escape_test.tsv
               ${DST_DIR}/hypertable_escape_test.tsv COPYONLY)
configure_file(${SRC_DIR}/hypertable_escape_test.golden
               ${DST_DIR}/hypertable_escape_test.golden COPYONLY)


# field_separator_test
configure_file(${SRC_DIR}/field_separator_test.tsv
               ${DST_DIR}/field_separator_test.tsv COPYONLY)
add_test(Alternate-field-separator env INSTALL_DIR=${INSTALL_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/field_separator_test.sh)

# delete_test
configure_file(${SRC_DIR}/delete_test.tsv
               ${DST_DIR}/delete_test.tsv COPYONLY)
add_test(Delete-tsv-test env INSTALL_DIR=${INSTALL_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/delete_test.sh)



if (NOT HT_COMPONENT_INSTALL)
  install(TARGETS ht_hypertable RUNTIME DESTINATION bin)
endif ()
