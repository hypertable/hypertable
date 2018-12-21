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

if (THRIFT_FOUND)
  add_custom_target(thriftdoc thrift -gen html -r
                    ${ThriftBroker_IDL_DIR}/Hql.thrift)
endif ()

add_custom_target(hqldoc ${CMAKE_SOURCE_DIR}/doc/bin/make-doc-tree.sh hqldoc)

add_custom_target(javadoc mvn -f java/pom.xml -Dmaven.test.skip=true -Papache2 javadoc:aggregate DEPENDS java)

if (DOXYGEN_FOUND)
  configure_file(${HYPERTABLE_SOURCE_DIR}/doc/Doxyfile
                 ${HYPERTABLE_BINARY_DIR}/Doxyfile)
  add_custom_target(doc ${DOXYGEN_EXECUTABLE} ${HYPERTABLE_BINARY_DIR}/Doxyfile)

  add_custom_command(TARGET doc PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory 
                     ${CMAKE_SOURCE_DIR}/doc/markdown markdown POST_BUILD COMMAND make hqldoc)

  if (THRIFT_FOUND)
    add_custom_command(TARGET doc POST_BUILD COMMAND make thriftdoc)
  endif ()

endif ()

