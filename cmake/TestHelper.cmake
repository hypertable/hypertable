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
# along with Hypertable. If not, see <http://www.gnu.org/licenses/>
#

set(INSTALLED_SERVERS
  ${INSTALL_DIR}/bin/htHyperspace
  ${INSTALL_DIR}/bin/htMaster
  ${INSTALL_DIR}/bin/htRangeServer
  ${INSTALL_DIR}/bin/htFsBrokerLocal
)

if (THRIFT_FOUND)
  set(INSTALLED_SERVERS ${INSTALLED_SERVERS} ${INSTALL_DIR}/bin/htThriftBroker)
endif ()

if (Ceph_FOUND)
  set(INSTALLED_SERVERS ${INSTALLED_SERVERS} ${INSTALL_DIR}/bin/htFsBrokerCeph)
endif ()

set(TEST_SERVERS_STARTED ${HYPERTABLE_BINARY_DIR}/test-servers-started)

add_custom_command(
  OUTPUT    ${TEST_SERVERS_STARTED}
  COMMAND   ${INSTALL_DIR}/bin/ht-start-test-servers.sh
  ARGS      --clear
  DEPENDS   ${INSTALLED_SERVERS}
  COMMENT   "Starting test servers"
)
add_custom_target(runtestservers DEPENDS ${TEST_SERVERS_STARTED})

macro(add_test_target target dir)
  add_custom_target(${target})
  add_dependencies(${target} runtestservers)
  add_custom_command(TARGET ${target} POST_BUILD COMMAND ${INSTALL_DIR}/bin/ht $(MAKE) test
                     WORKING_DIRECTORY ${dir})
endmacro()

# custom target must be globally unique to support IDEs like Xcode, VS etc.
add_test_target(alltests ${HYPERTABLE_BINARY_DIR})
add_test_target(coretests ${HYPERTABLE_BINARY_DIR}/src)
add_test_target(moretests ${HYPERTABLE_BINARY_DIR}/tests/integration)

add_custom_target(clear-test-env)
add_custom_command(TARGET clear-test-env COMMAND ${INSTALL_DIR}/bin/ht-destroy-database.sh)

