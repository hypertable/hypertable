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

# - Find Mapr
# Find the native Mapr includes and library
#
#  Mapr_INCLUDE_DIR - where to find Mapr.h, etc.
#  Mapr_LIBRARIES   - List of libraries when using Mapr.
#  Mapr_FOUND       - True if Mapr found.


find_path(Mapr_INCLUDE_DIR hdfs.h
	$ENV{HADOOP_HOME}/include
	/opt/mapr/hadoop/hadoop-0.20.2/src/c++/libhdfs
)

macro(FIND_MAPR_LIB lib)
  find_library(${lib}_LIB NAMES ${lib} PATHS 
	/opt/mapr/lib 
	$ENV{HADOOP_HOME}/lib/native
	)
  mark_as_advanced(${lib}_LIB)
endmacro(FIND_MAPR_LIB lib libname)

FIND_MAPR_LIB(hdfs)

if (Mapr_INCLUDE_DIR AND hdfs_LIB)
  set(Mapr_FOUND TRUE)
  
  if (jvm_LIB)
	set(Mapr_LIBRARIES ${hdfs_LIB} ${jvm_LIB})
	mark_as_advanced(Mapr_INCLUDE_DIR)
	message(STATUS "Found MAPR: ${Mapr_LIBRARIES}")
	HT_INSTALL_LIBS(lib ${hdfs_LIB})
  else ()
	if (FSBROKER_MAPR)
		message(FATAL_ERROR "Could NOT find jvm_LIB for MAPR libraries")
	endif ()
	set(Mapr_FOUND FALSE)
	set(Mapr_LIBRARIES)
  endif ()

else ()
   if (FSBROKER_MAPR)
      message(FATAL_ERROR "Could NOT find MAPR libraries")
   endif ()
   set(Mapr_FOUND FALSE)
   set(Mapr_LIBRARIES)
endif ()
