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

# Dependent libraries
HT_INSTALL_LIBS(lib ${Kfs_LIBRARIES})

# Apple specific
if (APPLE)
   install(FILES "/System/Library/Frameworks/CoreFoundation.framework/Versions/Current/CoreFoundation" DESTINATION lib)
   install(FILES "/System/Library/Frameworks/IOKit.framework/Versions/Current/IOKit" DESTINATION lib)
endif ()

# Need to include some "system" libraries as well
exec_program(${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh
             ARGS ${CMAKE_BINARY_DIR}/CMakeFiles/CompilerIdCXX/a.out
             OUTPUT_VARIABLE LDD_OUT RETURN_VALUE LDD_RETURN)

if (NOT LDD_RETURN STREQUAL "0")
  exec_program(${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh
               ARGS ${CMAKE_BINARY_DIR}/CMakeFiles/${CMAKE_VERSION}/CompilerIdCXX/a.out
               OUTPUT_VARIABLE LDD_OUT RETURN_VALUE LDD_RETURN)
endif ()

if (HT_CMAKE_DEBUG)
  message("ldd.sh output: ${LDD_OUT}")
endif ()

if (LDD_RETURN STREQUAL "0")
  string(REGEX MATCH "[ \t](/[^ ]+/libgcc_s\\.[^ \n]+)" dummy ${LDD_OUT})
  set(gcc_s_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libstdc\\+\\+\\.[^ \n]+)" dummy ${LDD_OUT})
  set(stdcxx_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libstacktrace\\.[^ \n]+)" dummy ${LDD_OUT})
  set(stacktrace_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libc\\+\\+\\.[^ \n]+)" dummy ${LDD_OUT})
  # Mac libraries
  set(cxx_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libc\\+\\+abi\\.[^ \n]+)" dummy ${LDD_OUT})
  set(cxxabi_lib ${CMAKE_MATCH_1})
  HT_INSTALL_LIBS(lib ${gcc_s_lib} ${stdcxx_lib} ${stacktrace_lib} ${cxx_lib} ${cxxabi_lib})
  
else ()
  set(LDD_CMD "${CMAKE_BINARY_DIR}/CMakeFiles/CompilerIdCXX/a.out")
  set(LDD_CMD "${CMAKE_SOURCE_DIR}/bin/src-utils/ldd.sh ${LDD_CMD}")
  message("ERROR: ${LDD_CMD} returned ${LDD_RETURN}")
  message(FATAL_ERROR "${LDD_OUT}")
endif ()




# General package variables
if (NOT CPACK_PACKAGE_NAME)
  set(CPACK_PACKAGE_NAME "hypertable")
endif ()

if (NOT CPACK_PACKAGE_CONTACT)
  set(CPACK_PACKAGE_CONTACT "info@hypertable.com")
endif ()

if (NOT CPACK_PACKAGE_DESCRIPTION_SUMMARY)
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Hypertable ${VERSION}")
endif ()

if (NOT CPACK_PACKAGE_VENDOR)
  set(CPACK_PACKAGE_VENDOR "hypertable.com")
endif ()

if (NOT CPACK_PACKAGE_DESCRIPTION_FILE)
  set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/doc/PACKAGE.txt")
endif ()

if (NOT CPACK_RESOURCE_FILE_LICENSE)
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
endif ()

set(CPACK_PACKAGE_VERSION ${VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_MICRO}.${VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CMAKE_INSTALL_PREFIX})

# packaging expecting i386 instead of i686 etc.
string(REGEX REPLACE "i[3-6]86" i386 MACH ${CMAKE_SYSTEM_PROCESSOR})

if (PACKAGE_OS_SPECIFIC)
  string(TOLOWER "${CPACK_PACKAGE_NAME}-${VERSION}-${OS_VERSION}-${MACH}"
         CPACK_PACKAGE_FILE_NAME)
else ()
  if (APPLE)
    string(REGEX MATCH "^([0-9]+)\\." dummy ${CMAKE_SYSTEM_VERSION})
    set(darwin_version ${CMAKE_MATCH_1})
    if (${darwin_version} EQUAL 10)
      set(system_name "osx_10.6")
    elseif (${darwin_version} EQUAL 11)
      set(system_name "osx_10.7")
    elseif (${darwin_version} EQUAL 12)
      set(system_name "osx_10.8")
    elseif (${darwin_version} EQUAL 13)
      set(system_name "osx_10.9")
    elseif (${darwin_version} EQUAL 14)
      set(system_name "osx_10.10")
    elseif (${darwin_version} EQUAL 15)
      set(system_name "osx_10.11")
    elseif (${darwin_version} EQUAL 16)
      set(system_name "osx_10.12")
    else ()
      message(FATAL_ERROR "Unknown OSX version (${CMAKE_SYSTEM})")
    endif ()
  else ()
    set(system_name ${CMAKE_SYSTEM_NAME})
  endif ()
  string(TOLOWER "${CPACK_PACKAGE_NAME}-${VERSION}-${system_name}-${MACH}"
         CPACK_PACKAGE_FILE_NAME)
endif ()

if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  string(TOLOWER "${CPACK_PACKAGE_FILE_NAME}-${CMAKE_BUILD_TYPE}"
         CPACK_PACKAGE_FILE_NAME)
endif ()

set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})

# Debian pakcage variables
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_BINARY_DIR}/prerm;${CMAKE_BINARY_DIR}/postinst;")

# RPM package variables

configure_file (${CMAKE_SOURCE_DIR}/bin/ht-rpm-pre-install.sh.in ${CMAKE_BINARY_DIR}/ht-rpm-pre-install.sh)
install(PROGRAMS ${CMAKE_BINARY_DIR}/ht-rpm-pre-install.sh DESTINATION bin)

configure_file (${CMAKE_SOURCE_DIR}/bin/ht-rpm-post-install.sh.in ${CMAKE_BINARY_DIR}/ht-rpm-post-install.sh)
install(PROGRAMS ${CMAKE_BINARY_DIR}/ht-rpm-post-install.sh DESTINATION bin)

set(CPACK_RPM_PACKAGE_LICENSE "GPLv3+")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Databases")
set(CPACK_RPM_PRE_INSTALL_SCRIPT_FILE ${CMAKE_INSTALL_PREFIX}/bin/ht-rpm-pre-install.sh)
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE ${CMAKE_INSTALL_PREFIX}/bin/ht-rpm-post-install.sh)

# rpm perl dependencies stuff is dumb
if (LANGS OR LANG_PL)
 set(CPACK_RPM_SPEC_MORE_DEFINE "
Provides: perl(Thrift)
Provides: perl(Thrift::BinaryProtocol)
Provides: perl(Thrift::FramedTransport)
Provides: perl(Thrift::Socket)
Provides: perl(Hypertable::ThriftGen2::HqlService)
Provides: perl(Hypertable::ThriftGen2::Types)
Provides: perl(Hypertable::ThriftGen::ClientService)
Provides: perl(Hypertable::ThriftGen::Types)
")
endif ()


include(CPack)
