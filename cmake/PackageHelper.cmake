# Copyright (C) 2007-2012 Hypertable, Inc.
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

macro(HT_GET_SONAME var fpath)
  exec_program(${CMAKE_SOURCE_DIR}/bin/soname.sh ARGS ${fpath}
               OUTPUT_VARIABLE SONAME_OUT RETURN_VALUE SONAME_RETURN)
  set(${var})

  if (SONAME_RETURN STREQUAL "0")
    set(${var} ${SONAME_OUT})
  endif ()

  if (NOT ${var})
    get_filename_component(${var} ${fpath} NAME)
  endif ()

  if (HT_CMAKE_DEBUG)
    message("SONAME: ${fpath} -> ${${var}}")
  endif ()

  # check if the library is prelinked, if so, warn
  exec_program(env ARGS objdump -h ${fpath} OUTPUT_VARIABLE ODH_OUT
               RETURN_VALUE ODH_RETURN)
  if (ODH_RETURN STREQUAL "0")
    string(REGEX MATCH "prelink_undo" match ${ODH_OUT})
    if (match)
      message("WARNING: ${fpath} is prelinked, RPMs may require --nomd5")
    endif ()
  endif ()
endmacro()

# This is a workaround for install() which always preserves symlinks
macro(HT_INSTALL_LIBS dest)
  if (INSTALL_EXCLUDE_DEPENDENT_LIBS)
    message(STATUS "Not installing dependent libraries")
  else ()
    foreach(fpath ${ARGN})
      if (NOT ${fpath} MATCHES "(NOTFOUND|\\.a)$")
        if (HT_CMAKE_DEBUG)
          message(STATUS "install copy: ${fpath}")
        endif ()
        HT_GET_SONAME(soname ${fpath})
        configure_file(${fpath} "${dest}/${soname}" COPYONLY)
        install(FILES "${CMAKE_BINARY_DIR}/${dest}/${soname}" DESTINATION ${dest})
      endif ()
    endforeach()
  endif ()
endmacro()

# Dependent libraries
HT_INSTALL_LIBS(lib ${BOOST_LIBS} ${Thrift_LIBS}
                ${Kfs_LIBRARIES} ${Mapr_LIBRARIES} ${LibEvent_LIB} ${Log4cpp_LIBRARIES}
                ${EXPAT_LIBRARIES} ${BZIP2_LIBRARIES}
                ${ZLIB_LIBRARIES} ${SNAPPY_LIBRARY} ${SIGAR_LIBRARY} ${Tcmalloc_LIBRARIES}
                ${Jemalloc_LIBRARIES} ${Ceph_LIBRARIES} ${RE2_LIBRARIES}
                ${EDITLINE_LIBRARIES})

if (NOT PACKAGE_THRIFTBROKER)
  HT_INSTALL_LIBS(lib ${BDB_LIBRARIES} ${RRD_LIBRARIES})
endif ()

# Need to include some "system" libraries as well
exec_program(${CMAKE_INSTALL_PREFIX}/bin/ldd.sh
             ARGS ${CMAKE_BINARY_DIR}/CMakeFiles/CompilerIdCXX/a.out
             OUTPUT_VARIABLE LDD_OUT RETURN_VALUE LDD_RETURN)

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
  HT_INSTALL_LIBS(lib ${gcc_s_lib} ${stdcxx_lib} ${stacktrace_lib})
endif ()

# copy cronolog to the /bin directory
install(PROGRAMS "${CRONOLOG_DIR}/cronolog" DESTINATION
      ${CMAKE_INSTALL_PREFIX}/bin)

exec_program(${CMAKE_INSTALL_PREFIX}/bin/ldd.sh
             ARGS ${CMAKE_BINARY_DIR}/src/cc/ThriftBroker/ThriftBroker
             OUTPUT_VARIABLE LDD_OUT RETURN_VALUE LDD_RETURN)

if (HT_CMAKE_DEBUG)
  message("ldd.sh output: ${LDD_OUT}")
endif ()

if (LDD_RETURN STREQUAL "0")
  string(REGEX MATCH "[ \t](/[^ ]+/libssl\\.[^ \n]+)" dummy ${LDD_OUT})
  set(ssl_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libgssapi_krb5\\.[^ \n]+)" dummy ${LDD_OUT})
  set(gssapi_krb5_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libkrb5\\.[^ \n]+)" dummy ${LDD_OUT})
  set(krb5_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libcom_err\\.[^ \n]+)" dummy ${LDD_OUT})
  set(com_err_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libk5crypto\\.[^ \n]+)" dummy ${LDD_OUT})
  set(k5crypto_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libcrypto\\.[^ \n]+)" dummy ${LDD_OUT})
  set(crypto_lib ${CMAKE_MATCH_1})
  string(REGEX MATCH "[ \t](/[^ ]+/libkrb5support\\.[^ \n]+)" dummy ${LDD_OUT})
  set(krb5support_lib ${CMAKE_MATCH_1})
endif ()

HT_INSTALL_LIBS(lib ${dbi_lib} ${directfb_lib} ${fusion_lib} ${direct_lib}
                ${xcb_render_util_lib} ${xcb_render_lib}
                ${pangocairo_lib} ${pango_lib} ${cairo_lib}
                ${fontconfig_lib} ${Xrender_lib} ${X11_lib} ${xml2_lib}
                ${pixman_lib} ${gobject_lib} ${gmodule_lib} ${glib_lib}
                ${pangoft2_lib} ${xcb_xlib_lib} ${xcb_lib} ${pcre_lib}
                ${Xau_lib} ${Xdmcp_lib} ${ssl_lib} ${gssapi_krb5_lib}
                ${krb5_lib} ${com_err_lib} ${k5crypto_lib} ${crypto_lib}
                ${krb5support_lib} ${Xrender_lib} ${rrd_lib} ${intl_lib}
                ${freetype_lib})

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
    "${CMAKE_BINARY_DIR}/prerm")

# RPM package variables
set(CPACK_RPM_PACKAGE_LICENSE "GPLv3+")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Databases")

# rpm perl dependencies stuff is dumb
set(CPACK_RPM_SPEC_MORE_DEFINE "
Provides: perl(Thrift)
Provides: perl(Thrift::BinaryProtocol)
Provides: perl(Thrift::FramedTransport)
Provides: perl(Thrift::Socket)
Provides: perl(Hypertable::ThriftGen2::HqlService)
Provides: perl(Hypertable::ThriftGen2::Types)
Provides: perl(Hypertable::ThriftGen::ClientService)
Provides: perl(Hypertable::ThriftGen::Types)

%preun
${CMAKE_INSTALL_PREFIX}/bin/prerm.sh")

include(CPack)
