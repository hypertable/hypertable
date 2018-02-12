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
#  Boost_INCLUDE_DIRS - the path to where the boost include files are.
#  Boost_LIBRARY_DIRS - The path to where the boost library files are.
#  Boost_LIB_DIAGNOSTIC_DEFINITIONS - Only set if using Windows.
#  BOOST_LIBS - Our default boost libs
#  BOOST_THREAD_LIB - The name of the boost thread library
#  BOOST_PROGRAM_OPTIONS_LIB - The name of the boost program options library
#  BOOST_FILESYSTEM_LIB - The name of the boost filesystem library
#  BOOST_IOSTREAMS_LIB - The name of the boost program options library
#  BOOST_PYTHON_LIB - The name of the boost python library
#  BOOST_SYSTEM_LIB - The name of the boost system library
#  BOOST_CHRONO_LIB - The name of the boost system library
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

if (NOT Boost_FIND_QUIETLY)
  message(STATUS "Looking for required boost libraries...")
endif ()

if (WIN32)
  set(Boost_LIB_DIAGNOSTIC_DEFINITIONS "-DBOOST_LIB_DIAGNOSTIC")
endif ()

# Hypertable Dependency Directory
if (NOT HT_DEPENDENCY_DIR)
  set(BOOST_DIR_SEARCH $ENV{BOOST_ROOT})
else ()
  set(BOOST_DIR_SEARCH ${HT_DEPENDENCY_DIR})
  set(Boost_INCLUDE_DIR ${HT_DEPENDENCY_DIR}/include)
endif ()

if (BOOST_DIR_SEARCH)
  file(TO_CMAKE_PATH ${BOOST_DIR_SEARCH} BOOST_DIR_SEARCH)
  set(BOOST_DIR_SEARCH ${BOOST_DIR_SEARCH}/include)
endif ()

if (WIN32)
  set(BOOST_DIR_SEARCH
    ${BOOST_DIR_SEARCH}
    C:/boost/include
    D:/boost/include
  )
endif ()

# Add in some path suffixes. These will have to be updated whenever a new
# Boost version comes out.
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

#
# Look for an installation.
#
if (NOT Boost_INCLUDE_DIR)
  find_path(Boost_INCLUDE_DIR
    NAMES boost/config.hpp
    PATH_SUFFIXES ${SUFFIX_FOR_PATH}
    # Look in other places.
    PATHS ${BOOST_DIR_SEARCH} /usr/include /usr/local/include /opt/local/include
    # Help the user find it if we cannot.
    DOC "The ${BOOST_INCLUDE_PATH_DESCRIPTION}"
  )
endif ()

macro(FIND_BOOST_PARENT root includedir)
  # Look for the boost library path.
  # Note that the user may not have installed any libraries
  # so it is quite possible the library path may not exist.
  set(${root} ${includedir})

  if (${root} MATCHES "boost-[0-9]+")
    get_filename_component(${root} ${${root}} PATH)
  endif ()

  if (${root} MATCHES "/include$")
    # Strip off the trailing "/include" in the path.
    get_filename_component(${root} ${${root}} PATH)
  endif ()
endmacro(FIND_BOOST_PARENT root includedir)

macro(FIND_BOOST_LIBRARY lib libname libroot required)
  set(${lib}_NAMES
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
  if (NOT "${ARGN}" STREQUAL "MT_ONLY")
    set(${lib}_NAMES ${${lib}_NAMES} "boost_${libname}")
  endif ()

  find_library(${lib} NO_DEFAULT_PATH
    NAMES ${${lib}_NAMES}
    PATHS "${libroot}/lib" "${libroot}/lib64" /lib /usr/lib /usr/local/lib /opt/local/lib
  )
  if (required AND ${lib} MATCHES "NOTFOUND$")
    message(FATAL_ERROR "required boost library: ${lib} not found")
  endif ()

  mark_as_advanced(${lib})
endmacro()

# Assume we didn't find it.
set(Boost_FOUND 0)

# Now try to get the include and library path.
if (Boost_INCLUDE_DIR)
  try_run(BOOST_CHECK SHOULD_COMPILE
          ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
          ${HYPERTABLE_SOURCE_DIR}/cmake/CheckBoost.cc
          CMAKE_FLAGS -DINCLUDE_DIRECTORIES:STRING=${Boost_INCLUDE_DIR}
          COMPILE_DEFINITIONS -v
          OUTPUT_VARIABLE BOOST_TRY_OUT)
  string(REGEX REPLACE ".*\n([0-9_]+).*" "\\1" BOOST_VERSION ${BOOST_TRY_OUT})
  string(REGEX REPLACE ".*\ngcc version ([0-9.]+).*" "\\1" GCC_VERSION
         ${BOOST_TRY_OUT})

  if (GCC_VERSION)
    message(STATUS "GCC version: ${GCC_VERSION}")
  endif ()

  message(STATUS "Boost version: ${BOOST_VERSION}")

  if (NOT BOOST_CHECK STREQUAL "0")
    message(FATAL_ERROR "Boost version not compatible")
  endif ()

  if (BOOST_VERSION MATCHES "1_34")
    set(Boost_HAS_SYSTEM_LIB false)
  else ()
    set(Boost_HAS_SYSTEM_LIB true)
  endif()

  FIND_BOOST_PARENT(Boost_PARENT ${Boost_INCLUDE_DIR})

  # Add boost libraries here
  FIND_BOOST_LIBRARY(BOOST_THREAD_LIB thread ${Boost_PARENT} true)
  get_filename_component(Boost_LIBRARY_DIR ${BOOST_THREAD_LIB} PATH)
  FIND_BOOST_LIBRARY(BOOST_PROGRAM_OPTIONS_LIB program_options
                     ${Boost_PARENT} true)
  FIND_BOOST_LIBRARY(BOOST_IOSTREAMS_LIB iostreams ${Boost_PARENT} true)
  FIND_BOOST_LIBRARY(BOOST_FILESYSTEM_LIB filesystem ${Boost_PARENT} true)
  
  if (LANGS OR LANG_PY2 OR LANG_PY3)
	FIND_BOOST_LIBRARY(BOOST_PYTHON_LIB python ${Boost_PARENT} false)
  endif()

  FIND_BOOST_LIBRARY(BOOST_CHRONO_LIB chrono ${Boost_PARENT} false)

  if(Boost_HAS_SYSTEM_LIB)
    FIND_BOOST_LIBRARY(BOOST_SYSTEM_LIB system ${Boost_PARENT} true)
  endif()

  if (APPLE)
    FIND_BOOST_LIBRARY(BOOST_SYSTEM_MT_LIB system-mt ${Boost_PARENT} false)
  endif()

  if (NOT Boost_FIND_QUIETLY)
    message(STATUS "Boost thread lib: ${BOOST_THREAD_LIB}")
    message(STATUS "Boost program options lib: ${BOOST_PROGRAM_OPTIONS_LIB}")
    message(STATUS "Boost filesystem lib: ${BOOST_FILESYSTEM_LIB}")
    message(STATUS "Boost iostreams lib: ${BOOST_IOSTREAMS_LIB}")
	
   if (LANGS OR LANG_PY2 OR LANG_PY3)
		message(STATUS "Boost python lib: ${BOOST_PYTHON_LIB}")
    endif()

    message(STATUS "Boost chrono lib: ${BOOST_CHRONO_LIB}")

    if(Boost_HAS_SYSTEM_LIB)
      message(STATUS "Boost system lib: ${BOOST_SYSTEM_LIB}")
    endif()

    message(STATUS "Boost lib dir: ${Boost_LIBRARY_DIR}")
  endif ()

  # BOOST_LIBS is our default boost libs.
  set(BOOST_LIBS ${BOOST_IOSTREAMS_LIB} ${BOOST_PROGRAM_OPTIONS_LIB}
      ${BOOST_FILESYSTEM_LIB} ${BOOST_THREAD_LIB})

  if(Boost_HAS_SYSTEM_LIB)
    set(BOOST_LIBS ${BOOST_LIBS} ${BOOST_SYSTEM_LIB} ${BOOST_SYSTEM_MT_LIB})
  endif()

  if (NOT ${BOOST_CHRONO_LIB} MATCHES "NOTFOUND$")
    set(BOOST_LIBS ${BOOST_LIBS} ${BOOST_CHRONO_LIB})
  endif ()

  if (LANGS OR LANG_PY2 OR LANG_PY3)
	if (PYTHON_FOUND AND NOT ${BOOST_PYTHON_LIB} MATCHES "NOTFOUND$")
		set(BOOST_LIBS ${BOOST_LIBS} ${BOOST_PYTHON_LIB} ${PYTHON_LIBRARY})
	endif ()
  endif ()

  message(STATUS "Boost libs: ${BOOST_LIBS}")

  if (EXISTS ${Boost_INCLUDE_DIR})
    set(Boost_INCLUDE_DIRS ${Boost_INCLUDE_DIR})
    # We have found boost. It is possible that the user has not
    # compiled any libraries so we set Boost_FOUND to be true here.
    set(Boost_FOUND 1)
  endif ()

  if (Boost_LIBRARY_DIR AND EXISTS "${Boost_LIBRARY_DIR}")
    set(Boost_LIBRARY_DIRS ${Boost_LIBRARY_DIR})
  endif ()
endif ()

if (NOT Boost_FOUND)
  if (Boost_FIND_REQUIRED)
    message(FATAL_ERROR "Required boost libraries not found")
  elseif (NOT Boost_FIND_QUIETLY)
    message(STATUS "Required boost libraries not found")
  endif ()
endif ()

mark_as_advanced(
  Boost_INCLUDE_DIR
)
