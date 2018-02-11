#
# Copyright (C) 2007-2016 Hypertable, Inc.
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
set(VERSION_MICRO "8")
set(VERSION_PATCH "11")
#set(VERSION_MISC_SUFFIX  "pre12")

# cmake behavior compatibility
cmake_minimum_required(VERSION 2.8)
mark_as_advanced(CMAKE_BACKWARDS_COMPATIBILITY)

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


#uname -p says i386, but we know better when its x86 apple
if (APPLE AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "i386")
    SET(CMAKE_SYSTEM_PROCESSOR "x86_64")
endif ()
#detect 32 or 64 bit compiler
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|x86_64)$")
    include(CheckTypeSize)
    check_type_size("void*" SIZEOF_VOID_P BUILTIN_TYPES_ONLY)
    if (${SIZEOF_VOID_P} EQUAL 8)
        set(CMAKE_SYSTEM_PROCESSOR_x86 64)
    else ()
        set(CMAKE_SYSTEM_PROCESSOR_x86 32)
    endif ()
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
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-long-long -pthreads")
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-long-long -pthreads")
  else (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-long-long")
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-long-long")
  endif (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
endif ()

# C flags
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-unknown-pragmas")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LARGEFILE_SOURCE")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_REENTRANT")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto -fuse-linker-plugin -ffat-lto-objects ")
# SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsection-anchors")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -floop-interchange")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64")
 
# C++ flags
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-unknown-pragmas")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_LARGEFILE_SOURCE")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FILE_OFFSET_BITS=64")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_REENTRANT")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto -fuse-linker-plugin -ffat-lto-objects")
# SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsection-anchors")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -floop-interchange")

if (ASAN)
  SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer")
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
endif ()

# Uncomment the following to build dynamic libraries
if (BUILD_SHARED_LIBS)
	set (BUILD_SHARED_LIBS ON)
	message(STATUS "Building shared libraries")
endif ()

# Locate required packages
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(CommonUtil)

# A more informative hypertable version string
include(VersionHelper)

# Verison install directory
set(CMAKE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX}/${VERSION})
set(INSTALL_DIR ${CMAKE_INSTALL_PREFIX})

if (NOT LANGS OR LANGS STREQUAL "all")
	set(LANGS ON)
	message(STATUS "Building for all supported languages")
else ()
	string(TOUPPER ${LANGS} LANGS)
	string(REPLACE "," ";" LANGS "${LANGS}")
	foreach(lg ${LANGS})
		SET("LANG_${lg}" ON)
		message(STATUS "Building for language: ${lg}")
	endforeach()
	set(LANGS OFF)
endif ()



find_package(Threads REQUIRED)
message(STATUS "Use thread library: ${CMAKE_THREAD_LIBS_INIT}")

if (LANGS OR LANG_JS)
	find_package(Nodejs)
endif ()

if (LANGS OR LANG_PY2 OR LANG_PY3 OR LANG_PYPY2 OR LANG_PYPY3)
	find_package(Python)
endif ()

find_package(SIGAR REQUIRED)
find_package(Boost REQUIRED)
find_package(BerkeleyDB REQUIRED)
find_package(EXPAT REQUIRED)
find_package(Editline REQUIRED)
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
find_package(Qfs)
find_package(Ceph)
find_package(Maven REQUIRED)
find_package(JNI)
find_package(LibEvent REQUIRED)
find_package(Libssh REQUIRED)
find_package(Libssl REQUIRED)
find_package(Thrift REQUIRED)

if (LANGS OR LANG_RB)
	find_package(RubyThrift)
endif ()
if (LANGS OR LANG_PHP)
	find_package(PHP5Thrift)
endif ()
if (LANGS OR LANG_PL)
	find_package(PerlThrift)
endif ()
if (LANGS OR LANG_PY2 OR LANG_PY3 OR LANG_PYPY2 OR LANG_PYPY3)
	find_package(PythonThrift)
endif ()


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
  if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR APPLE) OR USE_TCMALLOC)
    set(MALLOC_LIBRARY ${Tcmalloc_LIBRARIES})
    if (USE_TCMALLOC)
      SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTCMALLOC")
      SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTCMALLOC")
    else ()
      SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTCMALLOC_MINIMAL")
      SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTCMALLOC_MINIMAL")
    endif ()
  endif ()
endif ()
message(STATUS "Using MALLOC: " ${MALLOC_LIBRARY})


# Use llvm instead of gcc
if (LLVM)
  message(STATUS "Using llvm: ${LLVM}")
  SET (CMAKE_C_COMPILER             "${LLVM}/bin/clang")
  SET (CMAKE_C_FLAGS                "-Wall -Wno-unknown-pragmas -std=c99")
  if (ADDRESS_SANITIZER)
    SET (CMAKE_C_FLAGS_DEBUG        "-fsanitize=address -O1 -fno-omit-frame-pointer -g")
  else ()
    SET (CMAKE_C_FLAGS_DEBUG        "-g")
  endif ()
  SET (CMAKE_C_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
  SET (CMAKE_C_FLAGS_RELEASE        "-O3 -DNDEBUG")
  SET (CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")

  SET (CMAKE_CXX_COMPILER             "${LLVM}/bin/clang++")
  SET (CMAKE_CXX_FLAGS                "-Wall -Wno-unknown-pragmas")
  if (ADDRESS_SANITIZER)
    SET (CMAKE_CXX_FLAGS_DEBUG        "-fsanitize=address -O1 -fno-omit-frame-pointer -g")
  else ()
    SET (CMAKE_CXX_FLAGS_DEBUG        "-g")
  endif ()
  SET (CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
  SET (CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG")
  SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

  if (NOT APPLE)
    LINK_LIBRARIES(tinfo)
  endif ()

elseif (ADDRESS_SANITIZER)
  MESSAGE (FATAL_ERROR "ADDRESS_SANITIZER can only be used in conjunction with LLVM")
endif ()

if (APPLE)
   SET (CMAKE_EXE_LINKER_FLAGS "-framework IOKit -framework CoreFoundation")
endif ()

# include directories
include_directories(src/cc ${HYPERTABLE_BINARY_DIR}/src/cc
    ${ZLIB_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${Libssl_INCLUDE_DIR}
    ${EXPAT_INCLUDE_DIRS} ${BDB_INCLUDE_DIR} ${EDITLINE_INCLUDE_DIR}
    ${SIGAR_INCLUDE_DIR})

if (Thrift_FOUND)
  include_directories(${LibEvent_INCLUDE_DIR} ${Thrift_INCLUDE_DIR})
  SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHT_WITH_THRIFT")
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHT_WITH_THRIFT")
  set(ThriftBroker_IDL_DIR ${HYPERTABLE_SOURCE_DIR}/src/cc/ThriftBroker)
endif ()

if (GCC_VERSION MATCHES "^([4-9]|[1-9][0-9]+)\\.")
  SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-variadic-macros")
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-variadic-macros")
  # turn off deprecated feature warnings for gcc 4.3+
  if (GCC_VERSION MATCHES "^([4-9]|[1-9][0-9]+)\\.([3-9]|[1-9][0-9]+)")
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-deprecated")
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated")
  endif ()
elseif (GCC_VERSION MATCHES "^([4-9]|[1-9][0-9]+)\\.")
  message(STATUS "Ancient gcc detected, hope it finishes despite warnings...")
endif ()

include(HadoopHelper)
include(TestHelper)
include(DocHelper)

# sub-project directories
add_subdirectory(src)
add_subdirectory(java)
add_subdirectory(examples)
add_subdirectory(contrib)
add_subdirectory(tests/integration)

# Maven Repository
if (NOT MAVEN_REPOSITORY)
   SET (MAVEN_REPOSITORY "~/.m2/repository")
endif ()

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

# Copy Thrift files
if (THRIFT_SOURCE_DIR)
 if (LANGS OR LANG_PHP)
	message(STATUS "Copying PHP files into installation")
	file(GLOB PHPFILES ${HYPERTABLE_SOURCE_DIR}/src/php/*.php)
	install(FILES ${PHPFILES} DESTINATION lib/php)
	install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/php/lib/Thrift
			DESTINATION lib/php USE_SOURCE_PERMISSIONS)
 endif ()
 if (LANGS OR LANG_PL)
  message(STATUS "Copying Perl files into installation")
  file(GLOB PERLFILES ${THRIFT_SOURCE_DIR}/lib/perl/lib/*.pm)
  install(FILES ${PERLFILES} DESTINATION lib/perl)
  install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/perl/lib/Thrift
          DESTINATION lib/perl USE_SOURCE_PERMISSIONS)
 endif ()
 if (LANGS OR LANG_PY2 OR LANG_PY3 OR LANG_PYPY2 OR LANG_PYPY3)
  message(STATUS "Copying Python files into installation")
  install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/py/src/
          DESTINATION lib/py/thrift USE_SOURCE_PERMISSIONS)
 endif ()
 if (LANGS OR LANG_RB)
  message(STATUS "Copying Ruby files into installation")
  file(GLOB RUBYFILES ${THRIFT_SOURCE_DIR}/lib/rb/lib/*.rb)
  install(FILES ${RUBYFILES} DESTINATION lib/rb)
  install(DIRECTORY ${THRIFT_SOURCE_DIR}/lib/rb/lib/thrift
          DESTINATION lib/rb USE_SOURCE_PERMISSIONS)
 endif ()
endif ()

# Copy C++ Thrift files
install(DIRECTORY ${Thrift_INCLUDE_DIR} DESTINATION include USE_SOURCE_PERMISSIONS)

if (NOT HT_COMPONENT_INSTALL)
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
  install(PROGRAMS bin/ht-env.sh bin/fhsize.sh bin/rpm_pre_install.sh bin/rpm_post_install.sh DESTINATION bin)
endif ()

install(PROGRAMS "${CRONOLOG_DIR}/cronolog" DESTINATION sbin)

if (NODEJS_FOUND)
	install(PROGRAMS "${NODEJS_EXECUTABLE}" DESTINATION sbin)
endif ()

