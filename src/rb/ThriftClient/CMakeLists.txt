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

add_custom_target(ThriftGenRb ALL)
add_custom_command(
  TARGET   ThriftGenRb
  COMMAND  cp 
  ARGS     -r  ${CMAKE_CURRENT_SOURCE_DIR}/hypertable ${HYPERTABLE_BINARY_DIR}/src/rb/ThriftClient
)

#### GENERATE HYPERTHRIFT module
if (THRIFT_FOUND)
	add_custom_command(
		TARGET 		ThriftGenRb
		COMMAND   	thrift
		ARGS      	-r --gen rb -o ${HYPERTABLE_BINARY_DIR}/src/rb/ThriftClient/hypertable/
					${ThriftBroker_IDL_DIR}/Hql.thrift
		DEPENDS   	${ThriftBroker_IDL_DIR}/Client.thrift
					${ThriftBroker_IDL_DIR}/Hql.thrift
		COMMENT   	"thrift2rb"
	)
		
	#### minimize attr access
	add_custom_command(
		TARGET 	 ThriftGenRb
		COMMAND  sed 
		ARGS     -i 's/hypertable\\/thrift_gen\\/client_types/client_types/g'  ${HYPERTABLE_BINARY_DIR}/src/rb/ThriftClient/hypertable/gen-rb/hql_types.rb
	)
endif ()



add_test(ThriftClient-ruby ruby -I ${INSTALL_DIR}/lib/rb ${INSTALL_DIR}/lib/rb/client_test.rb)

if (NOT HT_COMPONENT_INSTALL OR PACKAGE_THRIFTBROKER)
  install(DIRECTORY ${HYPERTABLE_BINARY_DIR}/src/rb/ThriftClient/hypertable DESTINATION lib/rb)
  install(FILES client_test.rb DESTINATION lib/rb)
endif ()
