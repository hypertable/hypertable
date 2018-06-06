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



if (HDFS_VER MATCHES "^[0-1].[0-2]")
	## APACHE-HADOOP 1.0.0 to 1.2 DISTRIBUTION
	set(HDFS_VER_GROUP "apache-1.0_2")
	
elseif (HDFS_VER MATCHES "^2.[0-7]")
	## APACHE-HADOOP 2.0.0 to 2.7.5 DISTRIBUTION
	set(HDFS_VER_GROUP "apache-2.0_7")
	
endif ()


## CONSTRUCT MVN MODULE
if(HDFS_VER_GROUP)
	file(COPY ${HT_MVN_SRC_DIR}/fsbroker/hadoop/${HDFS_VER_GROUP}/ DESTINATION ${HT_MVN_DEST_DIR}/fsbroker-apache-${HDFS_VER})
	
	file(COPY ${HT_MVN_SRC_DIR}/fsbroker/hadoop/FsBrokerLib 
	     DESTINATION ${HT_MVN_DEST_DIR}/fsbroker-apache-${HDFS_VER}/src/main/java/org/hypertable/FsBroker)
	
	configure_file(${HT_MVN_DEST_DIR}/fsbroker-apache-${HDFS_VER}/pom.xml.in
				   ${HT_MVN_DEST_DIR}/fsbroker-apache-${HDFS_VER}/pom.xml @ONLY)
	set(MODULES_PLACE_HOLDER "${MODULES_PLACE_HOLDER} \n <module>fsbroker-apache-${HDFS_VER}</module>")
	
	if (JAVA_BUNDLED)
		install(FILES ${HT_MVN_DEST_DIR}/fsbroker-apache-${HDFS_VER}/target/ht-fsbroker-${VERSION}-apache-hadoop-${HDFS_VER}-bundled.jar
				DESTINATION lib/java/)
	else ()
		install(FILES ${HT_MVN_DEST_DIR}/fsbroker-apache-${HDFS_VER}/target/ht-fsbroker-${VERSION}-apache-hadoop-${HDFS_VER}.jar
				DESTINATION lib/java/${HDFS_VER_GROUP}/)
	endif ()
endif ()
			

# Dependencies if not bundled			
if (NOT JAVA_BUNDLED)

	if(HDFS_VER_GROUP STREQUAL "apache-1.0_2")
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-core/${HDFS_VER}/hadoop-core-${HDFS_VER}.jar
		        DESTINATION lib/java/${HDFS_VER_GROUP}/)
				
	elseif(HDFS_VER_GROUP STREQUAL "apache-2.0_7")
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-annotations/${HDFS_VER}/hadoop-annotations-${HDFS_VER}.jar
		        DESTINATION lib/java/${HDFS_VER_GROUP}/)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-auth/${HDFS_VER}/hadoop-auth-${HDFS_VER}.jar
		        DESTINATION lib/java/${HDFS_VER_GROUP}/)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-common/${HDFS_VER}/hadoop-common-${HDFS_VER}.jar
		        DESTINATION lib/java/${HDFS_VER_GROUP}/)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-hdfs/${HDFS_VER}/hadoop-hdfs-${HDFS_VER}.jar
		        DESTINATION lib/java/${HDFS_VER_GROUP}/)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/hadoop/hadoop-mapreduce-client-core/${HDFS_VER}/hadoop-mapreduce-client-core-${HDFS_VER}.jar
		        DESTINATION lib/java/${HDFS_VER_GROUP}/)
		install(FILES ${MAVEN_REPOSITORY}/org/apache/htrace/htrace-core/3.1.0-incubating/htrace-core-3.1.0-incubating.jar
		        DESTINATION lib/java/${HDFS_VER_GROUP}/)
	endif ()	
	#if(FSBROKERS OR FSBROKER_APACHE1 OR FSBROKER_APACHE2)
	#	install(FILES ${MAVEN_REPOSITORY}/com/google/protobuf/protobuf-java/2.5.0/protobuf-java-2.5.0.jar
	#			DESTINATION lib/java/specific)
	#endif ()	
endif ()


