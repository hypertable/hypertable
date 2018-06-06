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
include_directories(${Ceph_INCLUDE})
add_executable(htFsBrokerCeph main.cc CephBroker.cc)
target_link_libraries(htFsBrokerCeph HyperFsBroker ${Ceph_LIBRARIES})

install(TARGETS htFsBrokerCeph RUNTIME DESTINATION bin)
