# Copyright (C) 2007-2012 Hypertable, Inc.
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

if (Thrift_FOUND)
  HT_GLOB(ThriftGenJava_SRCS src/gen-java/*.java)

  add_custom_command(
    OUTPUT    ${ThriftGenJava_SRCS}
    COMMAND   thrift
    ARGS      -r -gen java -o ${HYPERTABLE_SOURCE_DIR}/src
              ${ThriftBroker_IDL_DIR}/Hql.thrift
    DEPENDS   ${ThriftBroker_IDL_DIR}/Client.thrift
              ${ThriftBroker_IDL_DIR}/Hql.thrift
    COMMENT   "thrift2java"
  )
endif ()

set(HADOOP_DISTRO "cdh3")
set(SOURCE_EXCLUDES "**/hadoop/2/*.java,**/hadoop/cdh5/*.java")
configure_file(${HYPERTABLE_SOURCE_DIR}/build.xml.in build1.xml @ONLY)
add_custom_target(Hadoop1JavaComponents ALL ant -f build1.xml
                  -Dbuild.dir=${HYPERTABLE_BINARY_DIR}/java jar
                  DEPENDS ${ThriftGenJava_SRCS})

add_custom_target(Hadoop1JavaExamples ALL ant -f build1.xml
                  -Dbuild.dir=${HYPERTABLE_BINARY_DIR}/java examples
                  DEPENDS ${ThriftGenJava_SRCS})
add_dependencies(Hadoop1JavaExamples Hadoop1JavaComponents)

set(HADOOP_DISTRO "cdh4")
set(SOURCE_EXCLUDES "**/hadoop/1/*.java,**/hadoop/cdh5/*.java")
configure_file(${HYPERTABLE_SOURCE_DIR}/build.xml.in build2.xml @ONLY)
add_custom_target(Hadoop2JavaComponents ALL ant -f build2.xml
                  -Dbuild.dir=${HYPERTABLE_BINARY_DIR}/java jar
                  DEPENDS ${ThriftGenJava_SRCS})
add_dependencies(Hadoop2JavaComponents Hadoop1JavaExamples)

add_custom_target(Hadoop2JavaExamples ALL ant -f build2.xml
                  -Dbuild.dir=${HYPERTABLE_BINARY_DIR}/java examples
                  DEPENDS ${ThriftGenJava_SRCS})
add_dependencies(Hadoop2JavaExamples Hadoop2JavaComponents)

set(HADOOP_DISTRO "cdh5")
set(SOURCE_EXCLUDES "**/hadoop/1/*.java,**/hadoop/2/*.java")
configure_file(${HYPERTABLE_SOURCE_DIR}/build.xml.in build-cdh5.xml @ONLY)
add_custom_target(CDH5JavaComponents ALL ant -f build-cdh5.xml
                  -Dbuild.dir=${HYPERTABLE_BINARY_DIR}/java jar
                  DEPENDS ${ThriftGenJava_SRCS})
add_dependencies(CDH5JavaComponents Hadoop2JavaExamples)

add_custom_target(CDH5JavaExamples ALL ant -f build-cdh5.xml
                  -Dbuild.dir=${HYPERTABLE_BINARY_DIR}/java examples
                  DEPENDS ${ThriftGenJava_SRCS})
add_dependencies(CDH5JavaExamples CDH5JavaComponents)

add_custom_target(java)
add_dependencies(java CDH5JavaExamples)

