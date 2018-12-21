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

# - Find the Boost includes and libraries.
# The following variables are set if Boost is found.  If Boost is not
# found, Boost_FOUND is set to false.
#  Boost_FOUND        - True when the Boost include directory is found.
#  BOOST_INCLUDE_DIRS - the path to where the boost include files are.
#  Boost_LIB_DIAGNOSTIC_DEFINITIONS - Only set if using Windows.
#  BOOST_LIBRARIES - Our default boost libs

# ----------------------------------------------------------------------------
#
# Usage:
# In your CMakeLists.txt file do something like this:
# ...
# # Boost
# find_package(Boost)
# ...
# include_directories(${Boost_INCLUDE_DIRS})

# TODO(llu) refactor into FindBoostLibs.cmake that uses stock FindBoost, which
# is greatly improved in cmake 2.6+

set(BOOST_LIBNAMES 
 system 
 filesystem 
 iostreams
 thread 
 chrono)

set(SUFFIX_FOR_PATH
 boost-1_44
 boost-1_43
 boost-1_42
 boost-1_41
 boost-1_40
 boost-1_39
 boost-1_38
 boost-1_37
 boost-1_36_1
 boost-1_36_0
 boost-1_35_1
 boost-1_35_0
 boost-1_35
 boost-1_34_1
 boost-1_34_0
)

# Hypertable Dependency Directory
if (NOT HT_DEPENDENCY_DIR)
  set(BOOST_DIR_SEARCH $ENV{BOOST_ROOT})
else ()
  set(BOOST_DIR_SEARCH ${HT_DEPENDENCY_DIR})
endif ()

if (WIN32)
  set(BOOST_DIR_SEARCH
    ${BOOST_DIR_SEARCH}
    C:/boost
    D:/boost
  )
  set(Boost_LIB_DIAGNOSTIC_DEFINITIONS "-DBOOST_LIB_DIAGNOSTIC")
endif ()


# Add in some path suffixes. These will have to be updated whenever a new
# Boost version comes out.
set(BOOST_LIBDIR_SEARCH "")
set(BOOST_DIR_SEARCH ${BOOST_DIR_SEARCH} /usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib /lib64 /lib)
foreach(path ${BOOST_DIR_SEARCH})
	foreach(path_suf ${SUFFIX_FOR_PATH})
		set(BOOST_LIBDIR_SEARCH ${BOOST_LIBDIR_SEARCH} ${path}/${path_suf})
	endforeach()
endforeach()
set(BOOST_LIBDIR_SEARCH ${BOOST_LIBDIR_SEARCH} /usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib /lib64 /lib)


set(BOOST_INCDIR_SEARCH "")
foreach(path_suf ${SUFFIX_FOR_PATH})
	set(BOOST_INCDIR_SEARCH ${BOOST_INCDIR_SEARCH} /usr/local/${path_suf}/include /usr/${path_suf}/include)
endforeach()
set(BOOST_INCDIR_SEARCH ${BOOST_INCDIR_SEARCH} /usr/local /usr)



# VARIATIONS FOR LIBRARY NAMES

	## IF HYPERPYTHON NOT DEPRECATED / NEED ATTENTION
if(LANG_PY_HYPERPYTHON)
	if(LANGS OR LANG_PY2)
		set(BOOST_LIBNAMES ${BOOST_LIBNAMES} "python")
	endif ()
	if(LANGS OR LANG_PY3)
		set(BOOST_LIBNAMES ${BOOST_LIBNAMES} "python3")
	endif ()
endif ()

SET(BOOST_STATIC_NAMES "")
SET(BOOST_SHARED_NAMES "")
foreach(libname ${BOOST_LIBNAMES})
	if(NOT HT_NOT_STATIC_CORE)
		set(BOOST_STATIC_NAMES ${BOOST_STATIC_NAMES}
			"libboost_${libname}.a"
			"libboost_${libname}-mt.a"
			"libboost_${libname}-gcc45-mt.a"
			"libboost_${libname}-gcc44-mt.a"
			"libboost_${libname}-gcc43-mt.a"
			"libboost_${libname}-gcc42-mt.a"
			"libboost_${libname}-gcc41-mt.a"
			"libboost_${libname}-gcc34-mt.a"
			"libboost_${libname}-xgcc40-mt.a"
		)	
	endif ()
	set(BOOST_SHARED_NAMES ${BOOST_SHARED_NAMES}
		"boost_${libname}"
		"boost_${libname}-mt"
		"boost_${libname}-gcc45-mt"
		"boost_${libname}-gcc44-mt"
		"boost_${libname}-gcc43-mt"
		"boost_${libname}-gcc42-mt"
		"boost_${libname}-gcc41-mt"
		"boost_${libname}-gcc34-mt"
		"boost_${libname}-xgcc40-mt"
	)	
endforeach()



HT_FASTLIB_SET(
	NAME "BOOST" 
	REQUIRED TRUE
	LIB_PATHS ${BOOST_INCDIR_SEARCH}
	INC_PATHS ${BOOST_LIBDIR_SEARCH}
	STATIC ${BOOST_STATIC_NAMES}
	SHARED ${BOOST_SHARED_NAMES}
	INCLUDE boost/config.hpp
)


if (BOOST_INCLUDE_DIR)
  try_run(BOOST_CHECK SHOULD_COMPILE
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckBoost.cc
          CMAKE_FLAGS -DINCLUDE_DIRECTORIES:STRING=${BOOST_INCLUDE_DIR}
          COMPILE_DEFINITIONS -v
          OUTPUT_VARIABLE BOOST_TRY_OUT)
  string(REGEX REPLACE ".*\n([0-9_]+).*" "\\1" BOOST_VERSION ${BOOST_TRY_OUT})
  string(REGEX REPLACE ".*\ngcc version ([0-9.]+).*" "\\1" GCC_VERSION
         ${BOOST_TRY_OUT})

  if (GCC_VERSION)
    message("       GCC version: ${GCC_VERSION}")
  endif ()
  message("       Boost version: ${BOOST_VERSION}")
endif ()

