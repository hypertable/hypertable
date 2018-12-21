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

string(TOLOWER ${CMAKE_SYSTEM_NAME} os)
INCLUDE_DIRECTORIES(sigar/os/${os})

set(ThirdParty_SRCS
	lzo/minilzo.c
	quicklz/quicklz.cc
	
	sigar/sigar_version.c
	sigar/os/${os}/${os}_sigar.c
	sigar/sigar.c
	sigar/sigar_cache.c
	sigar/sigar_fileinfo.c
	sigar/sigar_format.c
	sigar/sigar_getline.c
	sigar/sigar_ptql.c
	sigar/sigar_signal.c
	sigar/sigar_util.c
)

# lib HyperThirdParty
HT_ADD_LIBS(
	TARGET HyperThirdParty 
	SRCS ${ThirdParty_SRCS}
	)	

if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h)

  install(FILES ${HEADERS} ${VERSION_H} DESTINATION include/ThirdParty)

  install(TARGETS HyperThirdParty
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)

endif ()
