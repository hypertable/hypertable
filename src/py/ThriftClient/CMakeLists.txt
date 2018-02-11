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

HT_GLOB(GenPy_SRCS gen-py/*.py)

add_custom_command(
  OUTPUT    ${GenPy_SRCS}
  COMMAND   thrift
  ARGS      -r --gen py:new_style -o ${HYPERTABLE_BINARY_DIR}/src/py/ThriftClient/
            ${ThriftBroker_IDL_DIR}/Hql.thrift
  DEPENDS   ${ThriftBroker_IDL_DIR}/Client.thrift
            ${ThriftBroker_IDL_DIR}/Hql.thrift
  COMMENT   "thrift2py"
)

add_custom_target(ThriftGenPy ALL DEPENDS ${GenPy_SRCS})

if (PYTHONTHRIFT_FOUND)
  add_test(ThriftClient-python env
      PYTHONPATH=${HYPERTABLE_BINARY_DIR}/src/py
      python ${CMAKE_CURRENT_SOURCE_DIR}/client_test.py)
endif ()

if (NOT HT_COMPONENT_INSTALL OR PACKAGE_THRIFTBROKER)
  install(DIRECTORY 
     ${HYPERTABLE_BINARY_DIR}/src/py/ThriftClient/gen-py/hyperthrift 
     hypertable 
	 DESTINATION lib/py)
  install(FILES client_test.py MANIFEST.in README.rst DESTINATION lib/py)
  configure_file(setup.py.in ${HYPERTABLE_BINARY_DIR}/src/py/ThriftClient/setup.py @ONLY)
  install(FILES ${HYPERTABLE_BINARY_DIR}/src/py/ThriftClient/setup.py DESTINATION lib/py)

endif ()
