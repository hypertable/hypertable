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




## CDH 3 DISTRIBUTION
if (HDFS_VER MATCHES "^3")
	set(HDFS_VER "0.20.2-cdh3u5")
	
	file(COPY ${HT_MVN_SRC_DIR}/fsbroker/hadoop/cdh-3/ DESTINATION ${HT_MVN_DEST_DIR}/fsbroker-cdh-3)
	file(COPY ${HT_MVN_SRC_DIR}/fsbroker/hadoop/FsBrokerLib DESTINATION ${HT_MVN_DEST_DIR}/fsbroker-cdh-3/src/main/java/org/hypertable/FsBroker)
	
	configure_file(${HT_MVN_DEST_DIR}/fsbroker-cdh-3/pom.xml.in
					${HT_MVN_DEST_DIR}/fsbroker-cdh-3/pom.xml @ONLY)
	set(MODULES_PLACE_HOLDER "${MODULES_PLACE_HOLDER} \n <module>fsbroker-cdh-3</module>")
	
	install(FILES ${HT_MVN_DEST_DIR}/fsbroker-cdh-3/target/ht-fsbroker-${VERSION}-cdh-${HDFS_VER}-bundled.jar
            DESTINATION lib/java/)
	
	# CDH3 jars
    #if(NOT JAVA_BUNDLED)
	#	install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-core/${HDFS_VER}/hadoop-core-${HDFS_VER}.jar
    #   	    DESTINATION lib/java/cdh-3)
	#	install(FILES ${HT_MVN_DEST_DIR}/hypertable/target/hypertable-${VERSION}-cdh3.jar
    #   	 	DESTINATION lib/java/cdh-3)
	#	install(FILES ${HT_MVN_DEST_DIR}/hypertable-examples/target/hypertable-examples-${VERSION}-cdh3.jar
    #   	    DESTINATION lib/java/cdh-3)
	#	install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/thirdparty/guava/guava/r09-jarjar/guava-r09-jarjar.jar
    #           DESTINATION lib/java/cdh-3)
	# 	install(FILES ${MAVEN_REPOSITORY}/com/google/guava/guava/11.0.2/guava-11.0.2.jar
    #           DESTINATION lib/java/specific)
	#endif ()
endif ()

## CDH 4 DISTRIBUTION
if (HDFS_VER MATCHES "^4")
	set(HDFS_VER "2.0.0-cdh4.2.1")
	
	file(COPY ${HT_MVN_SRC_DIR}/fsbroker/hadoop/cdh-4.2.1/ DESTINATION ${HT_MVN_DEST_DIR}/fsbroker-cdh-4.2.1)
	file(COPY ${HT_MVN_SRC_DIR}/fsbroker/hadoop/FsBrokerLib DESTINATION ${HT_MVN_DEST_DIR}/fsbroker-cdh-4.2.1/src/main/java/org/hypertable/FsBroker)

	configure_file(${HT_MVN_DEST_DIR}/fsbroker-cdh-4.2.1/pom.xml.in
					${HT_MVN_DEST_DIR}/fsbroker-cdh-4.2.1/pom.xml @ONLY)
	set(MODULES_PLACE_HOLDER "${MODULES_PLACE_HOLDER} \n <module>fsbroker-cdh-4.2.1</module>")
	
	
	install(FILES ${HT_MVN_DEST_DIR}/fsbroker-cdh-4/target/ht-fsbroker-${VERSION}-cdh-${HDFS_VER}-bundled.jar
            DESTINATION lib/java/)
	# CDH4 jars
    #if(NOT JAVA_BUNDLED)
	#	install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-annotations/${HDFS_VER}/hadoop-annotations-${HDFS_VER}.jar
	#			DESTINATION lib/java/cdh4)
	#	install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-auth/${HDFS_VER}/hadoop-auth-${HDFS_VER}.jar
	#			DESTINATION lib/java/cdh4)
	#	install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-common/${HDFS_VER}/hadoop-common-${HDFS_VER}.jar
	#			DESTINATION lib/java/cdh4)
	#	install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-hdfs/${HDFS_VER}/hadoop-hdfs-${HDFS_VER}.jar
	#			DESTINATION lib/java/cdh4)
	#	install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-mapreduce-client-core/${HDFS_VER}/hadoop-mapreduce-client-core-${HDFS_VER}.jar
	#			DESTINATION lib/java/cdh4)
	#	install(FILES ${MAVEN_REPOSITORY}/com/google/protobuf/protobuf-java/2.4.0a/protobuf-java-2.4.0a.jar
    #   	    DESTINATION lib/java/specific)
	# 	install(FILES ${MAVEN_REPOSITORY}/com/google/guava/guava/11.0.2/guava-11.0.2.jar
    #           DESTINATION lib/java/specific)
	#endif ()
endif ()

