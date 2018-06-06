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

# - Find Maven (a java build tool)
# This module defines
#  MAVEN_VERSION version string of maven if found
#  MAVEN_FOUND, If false, do not try to use maven


if (NOT JAVA_INCLUDE_PATH OR JAVA_INCLUDE_PATH STREQUAL "")
    if (NOT $ENV{JAVA_INCLUDE_PATH})
		set(JAVA_INCLUDE_PATH "$ENV{JAVA_HOME}/include" )
	else ()
		set(JAVA_INCLUDE_PATH "$ENV{JAVA_INCLUDE_PATH}" )
	endif ()
endif ()
  
exec_program(env ARGS java -version OUTPUT_VARIABLE JAVARE_OUT
             RETURN_VALUE JAVARE_RETURN)

if (JAVARE_RETURN STREQUAL "0")
   set(JAVARE_FOUND TRUE) 
   string(REPLACE ";" " " JAVARE_OUT ${JAVARE_OUT})
   string(REPLACE "\n" ";" JAVARE_OUT ${JAVARE_OUT})
   list(GET JAVARE_OUT 0 JAVARE_OUT)
   message(STATUS "Java RE: ${JAVARE_OUT}")

else ()
   message(STATUS "Java RE: not found")
   set(JAVARE_FOUND FALSE)
endif ()



exec_program(env ARGS javac -version OUTPUT_VARIABLE JAVAC_OUT
             RETURN_VALUE JAVAC_RETURN)

if (JAVAC_RETURN STREQUAL "0")
  message(STATUS "Java Compiler: ${JAVAC_OUT}")
  message(STATUS "Java headers at: ${JAVA_INCLUDE_PATH}")
  
  if (NOT(JAVAC_OUT MATCHES "^javac 10." OR JAVAC_OUT MATCHES "^javac 9."))
	string(REGEX MATCH "1\\.[6-9]\\..*" JAVAC_VERSION ${JAVAC_OUT})
	if (NOT JAVAC_VERSION)
		message(STATUS "    Expected JDK 1.6 or greater. Skipping Java build")
		set(SKIP_JAVA_BUILD TRUE)
	endif ()
  endif ()
else ()
  message(STATUS "Java Compiler: not found")
  set(SKIP_JAVA_BUILD TRUE)
endif ()


macro(FIND_JAVA_LIB lib)
  find_library(${lib}_LIB NAMES ${lib} PATHS 
	$ENV{JAVA_HOME}/jre/lib/amd64
	$ENV{JAVA_HOME}/jre/lib/amd64/server  
	$ENV{JAVA_HOME}/lib/server
	$ENV{JAVA_HOME}/lib
	)
  if(${lib}_LIB)
	mark_as_advanced(${lib}_LIB)
  endif ()
endmacro(FIND_JAVA_LIB lib libname)

FIND_JAVA_LIB(jawt) # find_package(JNI)
FIND_JAVA_LIB(jvm)
FIND_JAVA_LIB(java)
FIND_JAVA_LIB(verify)

find_path(Jni_INCLUDE_DIR jni.h
	$ENV{JAVA_HOME}/include
)
if (jawt_LIB AND java_LIB AND Jni_INCLUDE_DIR)
  message(STATUS "Java AWT Native Interface: ${jawt_LIB} ${Jni_INCLUDE_DIR}")
else ()
  message(STATUS "Java AWT Native Interface: not found")
endif ()
if (jvm_LIB)
  set(jvm_LIB ${jvm_LIB} ${java_LIB} ${verify_LIB})
  message(STATUS "Java Virtual Machine: ${jvm_LIB}")
else ()
  message(STATUS "Java Virtual Machine: not found")
endif ()
