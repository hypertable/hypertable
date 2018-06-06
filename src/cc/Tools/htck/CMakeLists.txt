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

set(htck_SRCS ht_htck.cc MetadataDumpReader.cc RsmlReader.cc TableIdentifierMap.cc)

# htck - Hypertable consistency check and repair tool
add_executable(ht_htck ${htck_SRCS})
target_link_libraries(ht_htck Hypertable HyperRanger)

if (NOT HT_COMPONENT_INSTALL)
  install(TARGETS ht_htck RUNTIME DESTINATION bin)
endif ()
