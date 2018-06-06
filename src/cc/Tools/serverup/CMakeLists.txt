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

# hypertable - command interpreter
add_executable(ht_serverup serverup.cc)
if (THRIFT_FOUND)
  target_link_libraries(ht_serverup HyperFsBroker HyperThriftConfig HyperThrift Hyperspace Hypertable)
else ()
  target_link_libraries(ht_serverup HyperFsBroker Hyperspace Hypertable)
endif ()

install (TARGETS ht_serverup RUNTIME DESTINATION bin)
