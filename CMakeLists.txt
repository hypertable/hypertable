#
# Copyright (C) 2007-2012 Hypertable, Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

project(HYPERTABLE)

# Hypertable version components
set(VERSION_MAJOR "0")
set(VERSION_MINOR "9")
set(VERSION_MICRO "5")
set(VERSION_PATCH "6")
#set(VERSION_MISC_SUFFIX  "pre6")

# cmake behavior compatibility
cmake_minimum_required(VERSION 2.6.0)
mark_as_advanced(CMAKE_BACKWARDS_COMPATIBILITY)
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--no-as-needed")

# saner control structure syntax
set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS true)

if (COMMAND cmake_policy)
  # we prefer the more strict behavior, to find out more:
  # cmake --help-policy CMP0003
  cmake_policy(SET CMP0003 NEW)
endif ()

# Something nice for GUI like ccmake
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING
      "Options: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif ()

if (PHPTHRIFT_ROOT)
   message(FATAL_ERROR "PHPTHRIFT_ROOT variable no longer supported.  Use THRIFT_SOURCE_DIR instead.")
endif ()

# install directory prefix
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/opt/hypertable"
      CACHE PATH "installation directory prefix" FORCE)
endif ()

if (HT_DEPENDENCY_DIR)
  set(HT_DEPENDENCY_LIB_DIR ${HT_DEPENDENCY_DIR}/lib)
  set(HT_DEPENDENCY_INCLUDE_DIR ${HT_DEPENDENCY_DIR}/include)
endif ()

# Enable testing
enable_testing()

# A simple test helper
set(TEST_DIFF ${HYPERTABLE_SOURCE_DIR}/bin/src-utils/testdiff)

# gcc warning settings
if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
    add_definitions(-Wall -pedantic -Wno-long-long -pthreads)
  else (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
    add_definitions(-Wall -pedantic -Wno-long-long)
  endif (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
endif ()

add_definitions(-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_REENTRANT -fPIC)

# Uncomment the following to build dynamic libraries
# set (BUILD_SHARED_LIBS ON)

# Locate required packages
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(CommonUtil)

# A more informative hypertable version string
include(VersionHelper)

# Verison install directory
set(CMAKE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX}/${VERSION})
set(INSTALL_DIR ${CMAKE_INSTALL_PREFIX})

find_package(Threads REQUIRED)
message(STATUS "Use thread library: ${CMAKE_THREAD_LIBS_INIT}")
find_package(Boost REQUIRED)
find_package(BerkeleyDB REQUIRED)
find_package(EXPAT REQUIRED)
find_package(Log4cpp REQUIRED)
find_package(Editline REQUIRED)
find_package(SIGAR REQUIRED)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(RE2 REQUIRED)
find_package(Snappy REQUIRED)
find_package(RRDtool REQUIRED)
find_package(Cronolog REQUIRED)
find_package(Doxygen)
find_package(Tcmalloc)
find_package(Jemalloc)
find_package(Hoard)
find_package(Mapr)
find_package(Kfs)
find_package(Ceph)
find_package(Ant REQUIRED)
find_package(JNI)
find_package(PythonLibs)
find_package(LibEvent REQUIRED)
find_package(Thrift REQUIRED)
find_package(RubyThrift)
find_package(PHP5Thrift)
find_package(PerlThrift)
find_package(PythonThrift)
find_package(Python)

# set malloc library (maybe)
if (USE_JEMALLOC)
  if (Jemalloc_FOUND)
    set(MALLOC_LIBRARY ${Jemalloc_LIBRARIES})
  else ()
    message(FATAL_ERROR "Unable to use jemalloc: library not found")
  endif ()
elseif (USE_HOARD)
  if (Hoard_FOUND)
    set(MALLOC_LIBRARY ${Hoard_LIBRARIES})
  else ()
    message(FATAL_ERROR "Unable to use hoard malloc: library not found")
  endif ()
elseif (NOT USE_GLIBC_MALLOC AND Tcmalloc_FOUND)
  if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug" OR USE_TCMALLOC)
    set(MALLOC_LIBRARY ${Tcmalloc_LIBRARIES})
    if (USE_TCMALLOC)
      add_definitions(-DTCMALLOC)
    else ()
      add_definitions(-DTCMALLOC_MINIMAL)
    endif ()
  endif ()
endif ()

# include directories
include_directories(src/cc ${HYPERTABLE_BINARY_DIR}/src/cc
    ${ZLIB_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${Log4cpp_INCLUDE_DIR}
    ${EXPAT_INCLUDE_DIRS} ${BDB_INCLUDE_DIR} ${EDITLINE_INCLUDE_DIR}
    ${SIGAR_INCLUDE_DIR})

if (Thrift_FOUND)
  include_directories(${LibEvent_INCLUDE_DIR} ${Thrift_INCLUDE_DIR})
  add_definitions(-DHT_WITH_THRIFT)
  set(ThriftBroker_IDL_DIR ${HYPERTABLE_SOURCE_DIR}/src/cc/ThriftBroker)
endif ()

if (BOOST_VERSION MATCHES "1_34")
  message(STATUS "Got boost 1.34.x, prepend fix directory")
  include_directories(BEFORE src/cc/boost-1_34-fix)
endif ()

if (GCC_VERSION MATCHES "^([4-9]|[1-9][0-9]+)\\.")
  add_definitions(-Wno-variadic-macros)
  # turn off deprecated feature warnings for gcc 4.3+
  if (GCC_VERSION MATCHES "^([4-9]|[1-9][0-9]+)\\.([3-9]|[1-9][0-9]+)")
    add_definitions(-Wno-deprecated)
  endif ()
elseif (GCC_VERSION MATCHES "^([4-9]|[1-9][0-9]+)\\.")
  message(STATUS "Ancient gcc detected, hope it finishes despite warnings...")
endif ()

include(HadoopHelper)
include(TestHelper)
include(DocHelper)

# sub-project directories
add_subdirectory(src)
add_subdirectory(examples)
add_subdirectory(contrib)
add_subdirectory(tests/integration)

# Java stuff
if (NOT SKIP_JAVA_BUILD)
  include(JavaHelper)
endif ()

configure_file(bin/ht.in ht @ONLY)
configure_file(bin/postinst.in postinst @ONLY)
configure_file(bin/prerm.in prerm @ONLY)

file(GLOB CONFS conf/[^.]*)
install(FILES ${CONFS} DESTINATION conf)
install(PROGRAMS ${HYPERTABLE_BINARY_DIR}/ht DESTINATION bin)
file(GLOB JARS lib/*.jar lib/*.txt)
install(FILES ${JARS} DESTINATION lib/java)
install(FILES ${HYPERTABLE_BINARY_DIR}/java/hypertable-${VERSION}.jar
          DESTINATION lib/java OPTIONAL)
install(PROGRAMS bin/jrun DESTINATION bin)

# Copy Thrift files
if (THRIFT_SOURCE_DIR)
  message(STATUS "Copying PHP files into installation")
  file(GLOB PHPFILES ${THRIFT_SOURCE_DIR}/lib/php/src/*.php)
  install(FILES ${PHPFILES} DESTINATION lib/php)
  install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/php/src/protocol
		    ${THRIFT_SOURCE_DIR}/lib/php/src/server
		    ${THRIFT_SOURCE_DIR}/lib/php/src/transport
          DESTINATION lib/php USE_SOURCE_PERMISSIONS)
  message(STATUS "Copying Perl files into installation")
  file(GLOB PERLFILES ${THRIFT_SOURCE_DIR}/lib/perl/lib/*.pm)
  install(FILES ${PERLFILES} DESTINATION lib/perl)
  install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/perl/lib/Thrift
          DESTINATION lib/perl USE_SOURCE_PERMISSIONS)
endif ()

# Copy C++ Thrift files
install(DIRECTORY ${Thrift_INCLUDE_DIR} DESTINATION include USE_SOURCE_PERMISSIONS)
install(DIRECTORY ${Log4cpp_INCLUDE_DIR}/log4cpp DESTINATION include USE_SOURCE_PERMISSIONS)

if (NOT HT_COMPONENT_INSTALL)
  install(FILES ${HYPERTABLE_BINARY_DIR}/java/hypertable-${VERSION}-examples.jar
          DESTINATION lib/java OPTIONAL)
  file(GLOB SCRIPTS bin/*.sh)
  install(PROGRAMS ${SCRIPTS} DESTINATION bin)
  install(DIRECTORY examples/apache_log DESTINATION examples
          USE_SOURCE_PERMISSIONS)
  install(DIRECTORY examples/hql_tutorial DESTINATION examples
          USE_SOURCE_PERMISSIONS)
  install(DIRECTORY examples/freebase DESTINATION examples
          USE_SOURCE_PERMISSIONS)
  include(PackageHelper)
else ()
  install(PROGRAMS bin/ht-env.sh bin/fhsize.sh bin/prerm.sh DESTINATION bin)
endif ()

install(PROGRAMS "${CRONOLOG_DIR}/cronolog" DESTINATION bin)
