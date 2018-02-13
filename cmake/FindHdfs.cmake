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

# Legacy Hadoop helper

set(Hdfs_FOUND OFF)

if (JAVA_INCLUDE_PATH)
  
  if (NOT HADOOP_INCLUDE_PATH)
	set(HADOOP_INCLUDE_PATH $ENV{HADOOP_INCLUDE_PATH} )
  endif ()
  
  if (HADOOP_INCLUDE_PATH)
    message(STATUS "Hadoop includes located at: ${HADOOP_INCLUDE_PATH}")
  else ()
    message(STATUS "Please set HADOOP_INCLUDE_PATH variable for Legacy Hadoop support")
  endif(HADOOP_INCLUDE_PATH)

  if (NOT HADOOP_LIB_PATH)
	set(HADOOP_LIB_PATH $ENV{HADOOP_LIB_PATH} )
  endif ()
  if (HADOOP_LIB_PATH)
    message(STATUS "Hadoop libraries located at: ${HADOOP_LIB_PATH}")
  else ()
    message(STATUS "Please set HADOOP_LIB_PATH variable for Legacy Hadoop support")
  endif ()

  if (NOT BUILD_SHARED_LIBS)
    message(STATUS "Not building shared libraries. Legacy Hadoop support will be disabled")
  endif ()  

  exec_program(hadoop               
			ARGS version
            OUTPUT_VARIABLE HDFS_VERSION_OUT
            RETURN_VALUE HDFS_RETURN)
  if (HDFS_RETURN STREQUAL "0")
  
	  string(REPLACE "\n" " " HDFS_VERSION_OUT ${HDFS_VERSION_OUT})
	  string(REPLACE " " ";" HDFS_VERSION_OUT ${HDFS_VERSION_OUT})
	  list(GET HDFS_VERSION_OUT 0 HDFS_DIST)
	  list(GET HDFS_VERSION_OUT 1 HDFS_VERSION)
	
    if (NOT HDFS_VERSION MATCHES "^[0-9]+.*")
      set(HDFS_VERSION "unknown -- make sure it's N.N+")
    endif ()
    if (HDFS_DIST MATCHES "^Hadoop")
			string(SUBSTRING ${HDFS_VERSION} 0 1 HDFS_VERSION_TOP)
	      set(HDFS_DIST "apache${HDFS_VERSION_TOP}")
		  set(Hdfs_FOUND ON)
	  elseif (HDFS_DIST MATCHES "CDH" OR HDFS_DIST MATCHES "cloud") #not checked version parsing
	      set(HDFS_DIST "cdh")
		  set(Hdfs_FOUND ON)
	  endif ()
	
  else ()
     set(HDFS_VERSION  "not found")
     set(HDFS_DIST  "unknown")
  endif ()
	
  message(STATUS "       distribution: ${HDFS_DIST}")
  message(STATUS "       version: ${HDFS_VERSION}")

else ()
  message(STATUS "Java JNI not found. Legacy Hadoop support will be disabled.")
endif(JAVA_INCLUDE_PATH)

if(NOT Hdfs_FOUND AND FSBROKER_HDFS)
    message(FATAL_ERROR "Could NOT find HDFS libraries")
endif ()


