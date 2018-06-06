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

add_custom_target(ThriftGenPerl ALL)
add_custom_command(
  TARGET   ThriftGenPerl
  COMMAND  cp 
  ARGS     -r  ${CMAKE_CURRENT_SOURCE_DIR}/Hypertable ${HYPERTABLE_BINARY_DIR}/src/perl/ThriftClient
)

#### GENERATE HYPERTHRIFT module
if (THRIFT_FOUND)
	add_custom_command(
		TARGET 		ThriftGenPerl
		COMMAND   	thrift
		ARGS      	-r --gen perl -o ${HYPERTABLE_BINARY_DIR}/src/perl/ThriftClient/
					${ThriftBroker_IDL_DIR}/Hql.thrift
		DEPENDS   	${ThriftBroker_IDL_DIR}/Client.thrift
					${ThriftBroker_IDL_DIR}/Hql.thrift
		COMMENT   	"thrift2perl"
	)
endif ()


if (PERLTHRIFT_FOUND)
  add_test(ThriftClient-perl perl -I ${INSTALL_DIR}/lib/perl/ ${INSTALL_DIR}/lib/perl/client_test.pl)
endif ()

if (NOT HT_COMPONENT_INSTALL OR PACKAGE_THRIFTBROKER)
  install(DIRECTORY Hypertable DESTINATION lib/perl)
  install(DIRECTORY ${HYPERTABLE_BINARY_DIR}/src/perl/ThriftClient/gen-perl/Hypertable/ThriftGen 
					${HYPERTABLE_BINARY_DIR}/src/perl/ThriftClient/gen-perl/Hypertable/ThriftGen2 
		DESTINATION lib/perl/Hypertable)
  install(FILES client_test.pl DESTINATION lib/perl)
endif ()
