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

if (NODEJS_FOUND)

	add_custom_target(ThriftGenNODEJS ALL)
	add_custom_command(
		TARGET 		ThriftGenNODEJS
		COMMAND   	thrift
		ARGS      	 -r --gen js:node
					-o ${HYPERTABLE_BINARY_DIR}/src/js/
					${ThriftBroker_IDL_DIR}/Hql.thrift
		DEPENDS   	${ThriftBroker_IDL_DIR}/Client.thrift
					${ThriftBroker_IDL_DIR}/Hql.thrift
		COMMENT   	"ThriftPyGen"
	)

  if (NOT HT_COMONENT_INSTALL OR PACKAGE_THRIFTBROKER)
    install(DIRECTORY node DESTINATION lib/js)
    install(DIRECTORY ${HYPERTABLE_BINARY_DIR}/src/js/gen-nodejs/ DESTINATION lib/js/node/node_modules/hypertable/lib/hypertable/)
  endif ()

  add_test(NAME ThriftClient-nodejs
           WORKING_DIRECTORY ${INSTALL_DIR}/lib/js/node/
           COMMAND ${NODEJS_EXECUTABLE} ./client_test.js )

endif ()
