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


## Construct Maven project 
set(JAVA_BUNDLED ON)

set(HT_MVN_SRC_DIR  ${HYPERTABLE_SOURCE_DIR}/src/java/mvn)
set(HT_MVN_DEST_DIR  ${HYPERTABLE_BINARY_DIR}/src/java/mvn)

execute_process(COMMAND  rm -rf ${HT_MVN_DEST_DIR})
file(MAKE_DIRECTORY ${HT_MVN_DEST_DIR})

set(MODULES_PLACE_HOLDER "")


# JAVA HDFS FSBROKERS
if (FSBROKERS OR FSBROKER_HDFS)
	if(NOT HDFS_VERS)
		set(HDFS_VERS "${HDFS_DIST}-${HDFS_VERSION}")
	endif ()
	foreach(HDFS_VER ${HDFS_VERS})
		string(REPLACE "-" ";" HDFS_VER "${HDFS_VER}")
		list(GET HDFS_VER 0 ver_distro)
		list(REMOVE_AT HDFS_VER 0)
		set(HDFS_VER "${HDFS_VER}")
		include(hadoop_distro/${ver_distro}.cmake)
	endforeach()
endif ()



# JAVA THRIFTCLIENT
if (THRIFT_FOUND AND (LANGS OR LANG_JAVA))
	
	file(COPY ${HT_MVN_SRC_DIR}/thriftclient DESTINATION ${HT_MVN_DEST_DIR}/)
	configure_file(${HT_MVN_DEST_DIR}/thriftclient/pom.xml.in
					${HT_MVN_DEST_DIR}/thriftclient/pom.xml @ONLY)
	set(MODULES_PLACE_HOLDER "${MODULES_PLACE_HOLDER} \n <module>thriftclient</module>")
	

	install(FILES ${HT_MVN_DEST_DIR}/thriftclient/target/ht-thriftclient-${VERSION}-v${THRIFT_VERSION}-bundled.jar
            DESTINATION lib/java/)
	## install(FILES ${MAVEN_REPOSITORY}/org/apache/thrift/libthrift/${THRIFT_VERSION}/libthrift-${THRIFT_VERSION}.jar
    ##       	DESTINATION lib/java/common)
	
	
	file(COPY ${HT_MVN_SRC_DIR}/thriftclient-hadoop-tools DESTINATION ${HT_MVN_DEST_DIR}/)
	configure_file(${HT_MVN_DEST_DIR}/thriftclient-hadoop-tools/pom.xml.in
					${HT_MVN_DEST_DIR}/thriftclient-hadoop-tools/pom.xml @ONLY)
	set(MODULES_PLACE_HOLDER "${MODULES_PLACE_HOLDER} \n <module>thriftclient-hadoop-tools</module>")
		
	install(FILES ${HT_MVN_DEST_DIR}/thriftclient-hadoop-tools/target/ht-thriftclient-hadoop-tools-${VERSION}-v${THRIFT_VERSION}-bundled.jar
            DESTINATION lib/java/)
		
#		install(FILES ${HT_MVN_DEST_DIR}/examples/target/examples-${VERSION}-apache1.jar
#              DESTINATION lib/java/apache1)

	if (JAVARE_FOUND)
		add_test(ThriftClient-java
				 java -cp ${INSTALL_DIR}/lib/java/ht-thriftclient-${VERSION}-v${THRIFT_VERSION}-bundled.jar org.hypertable.thrift.BasicClientTest)
	endif ()

endif ()



# BUILD JAVA WITH MAVEN
if( NOT MODULES_PLACE_HOLDER STREQUAL "")

	file(COPY ${HT_MVN_SRC_DIR}/common DESTINATION ${HT_MVN_DEST_DIR}/)
	configure_file(${HT_MVN_SRC_DIR}/common/pom.xml.in  ${HT_MVN_DEST_DIR}/common/pom.xml @ONLY)
	set(MODULES_PLACE_HOLDER "${MODULES_PLACE_HOLDER} \n <module>common</module>")
	
	
	configure_file(${HT_MVN_SRC_DIR}/pom.xml.in  ${HT_MVN_DEST_DIR}/pom.xml @ONLY)
	file(COPY ${HT_MVN_SRC_DIR}/assembly.xml DESTINATION ${HT_MVN_DEST_DIR}/)
	
	add_custom_target(MVN_build ALL 
					  mvn -f mvn/pom.xml -Dmaven.test.skip=true 
				      package)
	install(FILES ${HT_MVN_DEST_DIR}/common/target/ht-common-${VERSION}-bundled.jar
            DESTINATION lib/java/)
	if (THRIFT_FOUND AND (LANGS OR LANG_JAVA))
		add_dependencies(MVN_build ThriftGenJava)
	endif ()
	
endif ()



# Common jars
if(NOT JAVA_BUNDLED)

	install(FILES ${MAVEN_REPOSITORY}/commons-cli/commons-cli/1.2/commons-cli-1.2.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/commons-collections/commons-collections/3.2.1/commons-collections-3.2.1.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/commons-configuration/commons-configuration/1.6/commons-configuration-1.6.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/commons-io/commons-io/2.4/commons-io-2.4.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/commons-lang/commons-lang/2.6/commons-lang-2.6.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/commons-logging/commons-logging/1.1.1/commons-logging-1.1.1.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/log4j/log4j/1.2.17/log4j-1.2.17.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/org/slf4j/slf4j-api/1.7.5/slf4j-api-1.7.5.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/org/slf4j/slf4j-log4j12/1.7.5/slf4j-log4j12-1.7.5.jar
              DESTINATION lib/java/common)
	install(FILES ${MAVEN_REPOSITORY}/org/fusesource/sigar/1.6.4/sigar-1.6.4.jar
              DESTINATION lib/java/common)

endif ()

