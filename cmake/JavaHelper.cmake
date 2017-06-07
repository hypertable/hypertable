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

# Copy Maven source tree to build directory
file(COPY java DESTINATION ${HYPERTABLE_BINARY_DIR} PATTERN pom.xml.in EXCLUDE)

# Top-level pom.xml
configure_file(${HYPERTABLE_SOURCE_DIR}/java/pom.xml.in ${HYPERTABLE_BINARY_DIR}/java/pom.xml @ONLY)

# runtime-dependencies/pom.xml
configure_file(${HYPERTABLE_SOURCE_DIR}/java/runtime-dependencies/pom.xml.in
         ${HYPERTABLE_BINARY_DIR}/java/runtime-dependencies/pom.xml @ONLY)

# runtime-dependencies/common/pom.xml
configure_file(${HYPERTABLE_SOURCE_DIR}/java/runtime-dependencies/common/pom.xml.in
         ${HYPERTABLE_BINARY_DIR}/java/runtime-dependencies/common/pom.xml @ONLY)

#add_custom_target(CleanHadoopBuild ALL rm -rf ${HYPERTABLE_BINARY_DIR}/java/hypertable-common/target/classes
#      COMMAND rm -rf ${HYPERTABLE_BINARY_DIR}/java/hypertable-examples/target/classes
#     )
if (HDFS_DIST MATCHES "^apache")

	#set(APACHE1_VERSION "1.2.1")
	#set(APACHE2_VERSION "2.4.1")
	
	# runtime-dependencies/${HDFS_DIST}/pom.xml
	configure_file(${HYPERTABLE_SOURCE_DIR}/java/runtime-dependencies/${HDFS_DIST}/pom.xml.in
		${HYPERTABLE_BINARY_DIR}/java/runtime-dependencies/${HDFS_DIST}/pom.xml @ONLY)
			
	# hypertable-${HDFS_DIST}/pom.xml
	configure_file(${HYPERTABLE_SOURCE_DIR}/java/hypertable-${HDFS_DIST}/pom.xml.in
		${HYPERTABLE_BINARY_DIR}/java/hypertable-${HDFS_DIST}/pom.xml @ONLY)

    if (HDFS_VERSION MATCHES "^1.")
	elseif (HDFS_VERSION MATCHES "^2.")
	endif ()
 
elseif (HDFS_DIST MATCHES "cdh")    

	if (HDFS_VERSION MATCHES "^0.2")
		set(CDH3_VERSION "0.20.2-cdh3u5")
		# runtime-dependencies/cdh3/pom.xml
		configure_file(${HYPERTABLE_SOURCE_DIR}/java/runtime-dependencies/cdh3/pom.xml.in
			${HYPERTABLE_BINARY_DIR}/java/runtime-dependencies/cdh3/pom.xml @ONLY)
		# hypertable-cdh3/pom.xml
		configure_file(${HYPERTABLE_SOURCE_DIR}/java/hypertable-cdh3/pom.xml.in
			${HYPERTABLE_BINARY_DIR}/java/hypertable-cdh3/pom.xml @ONLY)

	elseif (HDFS_VERSION MATCHES "^2.")
		set(CDH4_VERSION "2.0.0-cdh4.2.1")

		# runtime-dependencies/cdh4/pom.xml
		configure_file(${HYPERTABLE_SOURCE_DIR}/java/runtime-dependencies/cdh4/pom.xml.in
			${HYPERTABLE_BINARY_DIR}/java/runtime-dependencies/cdh4/pom.xml @ONLY				  
                  DEPENDS RuntimeDependencies)

	endif ()

endif ()

# hypertable-common/pom.xml
configure_file(${HYPERTABLE_SOURCE_DIR}/java/hypertable-common/pom.xml.in
         ${HYPERTABLE_BINARY_DIR}/java/hypertable-common/pom.xml @ONLY)

# hypertable/pom.xml
configure_file(${HYPERTABLE_SOURCE_DIR}/java/hypertable/pom.xml.in
         ${HYPERTABLE_BINARY_DIR}/java/hypertable/pom.xml @ONLY)

# hypertable-examples/pom.xml
configure_file(${HYPERTABLE_SOURCE_DIR}/java/hypertable-examples/pom.xml.in
         ${HYPERTABLE_BINARY_DIR}/java/hypertable-examples/pom.xml @ONLY)

		 
		 
if (Thrift_FOUND)

  set(ThriftGenJava_DIR ${HYPERTABLE_BINARY_DIR}/java/hypertable-common/src/main/java/org/hypertable/thriftgen)

  set(ThriftGenJava_SRCS
      ${ThriftGenJava_DIR}/AccessGroupOptions.java
      ${ThriftGenJava_DIR}/AccessGroupSpec.java
      ${ThriftGenJava_DIR}/CellInterval.java
      ${ThriftGenJava_DIR}/Cell.java
      ${ThriftGenJava_DIR}/ClientException.java
      ${ThriftGenJava_DIR}/ClientService.java
      ${ThriftGenJava_DIR}/ColumnFamilyOptions.java
      ${ThriftGenJava_DIR}/ColumnFamilySpec.java
      ${ThriftGenJava_DIR}/ColumnPredicate.java
      ${ThriftGenJava_DIR}/ColumnPredicateOperation.java
      ${ThriftGenJava_DIR}/HqlResult2.java
      ${ThriftGenJava_DIR}/HqlResultAsArrays.java
      ${ThriftGenJava_DIR}/HqlResult.java
      ${ThriftGenJava_DIR}/HqlService.java
      ${ThriftGenJava_DIR}/KeyFlag.java
      ${ThriftGenJava_DIR}/Key.java
      ${ThriftGenJava_DIR}/MutateSpec.java
      ${ThriftGenJava_DIR}/MutatorFlag.java
      ${ThriftGenJava_DIR}/NamespaceListing.java
      ${ThriftGenJava_DIR}/ResultAsArrays.java
      ${ThriftGenJava_DIR}/Result.java
      ${ThriftGenJava_DIR}/ResultSerialized.java
      ${ThriftGenJava_DIR}/RowInterval.java
      ${ThriftGenJava_DIR}/ScanSpec.java
      ${ThriftGenJava_DIR}/Schema.java
      ${ThriftGenJava_DIR}/TableSplit.java
  )

  add_custom_command(
    OUTPUT    ${ThriftGenJava_SRCS}
    COMMAND   thrift
    ARGS      -r -gen java -out ${HYPERTABLE_BINARY_DIR}/java/hypertable-common/src/main/java
              ${ThriftBroker_IDL_DIR}/Hql.thrift
    DEPENDS   ${ThriftBroker_IDL_DIR}/Client.thrift
              ${ThriftBroker_IDL_DIR}/Hql.thrift
    COMMENT   "thrift2java"
  )
endif ()


if (HDFS_DIST MATCHES "^apache")

	add_custom_target(HypertableHadoop ALL mvn -f java/pom.xml -Dmaven.test.skip=true -P ${HDFS_DIST} package
            DEPENDS ${ThriftGenJava_SRCS})

 
elseif (HDFS_DIST MATCHES "cdh")    

	if (HDFS_VERSION MATCHES "^0.2")
		set(CDH3_VERSION "0.20.2-cdh3u5")

		add_custom_target(HypertableHadoop ALL mvn -f java/pom.xml -Dmaven.test.skip=true -P ${HDFS_DIST}3 package
                  DEPENDS ${ThriftGenJava_SRCS})

	elseif (HDFS_VERSION MATCHES "^2.")
		set(CDH4_VERSION "2.0.0-cdh4.2.1")

		# runtime-dependencies/cdh4/pom.xml
		configure_file(${HYPERTABLE_SOURCE_DIR}/java/runtime-dependencies/cdh4/pom.xml.in
			${HYPERTABLE_BINARY_DIR}/java/runtime-dependencies/cdh4/pom.xml @ONLY				  
                  DEPENDS RuntimeDependencies)
			

	endif ()
endif ()

add_custom_target(CleanBuild0 ALL rm -rf ${HYPERTABLE_BINARY_DIR}/java/hypertable-common/target/classes
                  COMMAND rm -rf ${HYPERTABLE_BINARY_DIR}/java/hypertable-examples/target/classes
                  DEPENDS HypertableHadoop)

add_custom_target(RuntimeDependencies ALL mvn -f java/runtime-dependencies/pom.xml -Dmaven.test.skip=true package
                  DEPENDS HypertableHadoop)
				  
add_custom_target(java)
add_dependencies(java RuntimeDependencies)






# Common jars
install(FILES ${MAVEN_REPOSITORY}/org/apache/thrift/libthrift/${Thrift_VERSION}/libthrift-${Thrift_VERSION}.jar
              DESTINATION lib/java/common)
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

# Distro specific jars
install(FILES ${MAVEN_REPOSITORY}/com/google/guava/guava/11.0.2/guava-11.0.2.jar
              DESTINATION lib/java/specific)


if (HDFS_DIST MATCHES "^apache")

	install(FILES ${MAVEN_REPOSITORY}/com/google/protobuf/protobuf-java/2.5.0/protobuf-java-2.5.0.jar
              DESTINATION lib/java/specific)
	#set(APACHE1_VERSION "1.2.1")
	#set(APACHE2_VERSION "2.4.1")
	
    if (HDFS_VERSION MATCHES "^1.")
		# Apache Hadoop 1 jars
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-core/${HDFS_VERSION}/hadoop-core-${HDFS_VERSION}.jar
              DESTINATION lib/java/apache1)
		install(FILES ${HYPERTABLE_BINARY_DIR}/java/hypertable/target/hypertable-${VERSION}-apache1.jar
              DESTINATION lib/java/apache1)
		install(FILES ${HYPERTABLE_BINARY_DIR}/java/hypertable-examples/target/hypertable-examples-${VERSION}-apache1.jar
              DESTINATION lib/java/apache1)

	elseif (HDFS_VERSION MATCHES "^2.")
		# Apache Hadoop 2 jars
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-annotations/${HDFS_VERSION}/hadoop-annotations-${HDFS_VERSION}.jar
              DESTINATION lib/java/apache2)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/htrace/htrace-core/3.1.0-incubating/htrace-core-3.1.0-incubating.jar
              DESTINATION lib/java/apache2)

		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-auth/${HDFS_VERSION}/hadoop-auth-${HDFS_VERSION}.jar
              DESTINATION lib/java/apache2)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-common/${HDFS_VERSION}/hadoop-common-${HDFS_VERSION}.jar
              DESTINATION lib/java/apache2)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-hdfs/${HDFS_VERSION}/hadoop-hdfs-${HDFS_VERSION}.jar
              DESTINATION lib/java/apache2)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-mapreduce-client-core/${HDFS_VERSION}/hadoop-mapreduce-client-core-${HDFS_VERSION}.jar
              DESTINATION lib/java/apache2)
		install(FILES ${HYPERTABLE_BINARY_DIR}/java/hypertable/target/hypertable-${VERSION}-apache2.jar
              DESTINATION lib/java/apache2)
		install(FILES ${HYPERTABLE_BINARY_DIR}/java/hypertable-examples/target/hypertable-examples-${VERSION}-apache2.jar
              DESTINATION lib/java/apache2)
	endif ()
 
elseif (HDFS_DIST MATCHES "cdh")    
	install(FILES ${MAVEN_REPOSITORY}/com/google/protobuf/protobuf-java/2.4.0a/protobuf-java-2.4.0a.jar
              DESTINATION lib/java/specific)

	if (HDFS_VERSION MATCHES "^0.2")
		set(CDH3_VERSION "0.20.2-cdh3u5")
		# CDH3 jars
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-core/${CDH3_VERSION}/hadoop-core-${CDH3_VERSION}.jar
              DESTINATION lib/java/cdh3)
		install(FILES ${HYPERTABLE_BINARY_DIR}/java/hypertable/target/hypertable-${VERSION}-cdh3.jar
              DESTINATION lib/java/cdh3)
		install(FILES ${HYPERTABLE_BINARY_DIR}/java/hypertable-examples/target/hypertable-examples-${VERSION}-cdh3.jar
              DESTINATION lib/java/cdh3)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/thirdparty/guava/guava/r09-jarjar/guava-r09-jarjar.jar
              DESTINATION lib/java/cdh3)

	elseif (HDFS_VERSION MATCHES "^2.")
		set(CDH4_VERSION "2.0.0-cdh4.2.1")
		# CDH4 jars
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-annotations/${CDH4_VERSION}/hadoop-annotations-${CDH4_VERSION}.jar
              DESTINATION lib/java/cdh4)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-auth/${CDH4_VERSION}/hadoop-auth-${CDH4_VERSION}.jar
              DESTINATION lib/java/cdh4)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-common/${CDH4_VERSION}/hadoop-common-${CDH4_VERSION}.jar
              DESTINATION lib/java/cdh4)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-hdfs/${CDH4_VERSION}/hadoop-hdfs-${CDH4_VERSION}.jar
             DESTINATION lib/java/cdh4)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-mapreduce-client-core/${CDH4_VERSION}/hadoop-mapreduce-client-core-${CDH4_VERSION}.jar
              DESTINATION lib/java/cdh4)

	endif ()
endif ()
