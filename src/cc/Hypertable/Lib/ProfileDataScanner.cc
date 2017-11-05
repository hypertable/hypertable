/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3
 * of the License.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/// @file
/// Ddefinitions for ProfileDataScanner.
/// This file contains type ddefinitions for ProfileDataScanner, a calls that
/// holds profile information for a scanner request.

#include <Common/Compat.h>

#include "ProfileDataScanner.h"

#include <Common/Serialization.h>
#include <Common/StringExt.h>

using namespace Hypertable;
using namespace Hypertable::Serialization;
using namespace std;

uint8_t ProfileDataScanner::encoding_version() const {
  return 2;
}

size_t ProfileDataScanner::encoded_length_internal() const {
  size_t length = 60;
  if (!servers.empty()) {
    for (auto & str : servers)
      length += encoded_length_vstr(str);
  }
  return length;
}

void ProfileDataScanner::encode_internal(uint8_t **bufp) const {
  encode_i32(bufp, (uint32_t)subscanners);
  encode_i32(bufp, (uint32_t)scanblocks);
  encode_i64(bufp, (uint64_t)cells_scanned);
  encode_i64(bufp, (uint64_t)cells_returned);
  encode_i64(bufp, (uint64_t)bytes_scanned);
  encode_i64(bufp, (uint64_t)bytes_returned);
  encode_i64(bufp, (uint64_t)disk_read);
  encode_i32(bufp, (uint32_t)servers.size());
  if (!servers.empty()) {
    for (auto & str : servers)
      encode_vstr(bufp, str);
  }
  encode_i64(bufp, (uint64_t)elapsed_time_millis);
}

void ProfileDataScanner::decode_internal(uint8_t version, const uint8_t **bufp,
					 size_t *remainp) {
  subscanners = (int32_t)decode_i32(bufp, remainp);
  scanblocks = (int32_t)decode_i32(bufp, remainp);
  cells_scanned = (int64_t)decode_i64(bufp, remainp);
  cells_returned = (int64_t)decode_i64(bufp, remainp);
  bytes_scanned = (int64_t)decode_i64(bufp, remainp);
  bytes_returned = (int64_t)decode_i64(bufp, remainp);
  disk_read = (int64_t)decode_i64(bufp, remainp);
  size_t count = (size_t)decode_i32(bufp, remainp);
  for (size_t i=0; i<count; i++)
    servers.insert( decode_vstr(bufp, remainp) );
  if (version >= 2)
    elapsed_time_millis = (int64_t)decode_i64(bufp, remainp);
}


ProfileDataScanner &ProfileDataScanner::operator+=(const ProfileDataScanner &other) {
  subscanners += other.subscanners;
  scanblocks += other.scanblocks;
  cells_scanned += other.cells_scanned;
  cells_returned += other.cells_returned;
  bytes_scanned += other.bytes_scanned;
  bytes_returned += other.bytes_returned;
  disk_read += other.disk_read;
  servers.insert(other.servers.begin(), other.servers.end());
  elapsed_time_millis += other.elapsed_time_millis;
  return *this;
}

ProfileDataScanner &ProfileDataScanner::operator-=(const ProfileDataScanner &other) {
  subscanners -= other.subscanners;
  scanblocks -= other.scanblocks;
  cells_scanned -= other.cells_scanned;
  cells_returned -= other.cells_returned;
  bytes_scanned -= other.bytes_scanned;
  bytes_returned -= other.bytes_returned;
  disk_read -= other.disk_read;
  for (auto &server : other.servers)
    servers.erase(server);
  elapsed_time_millis -= other.elapsed_time_millis;
  return *this;
}

string ProfileDataScanner::to_string() {
  string str = "{ProfileDataScanner: ";
  str += string("elapsed_time_millis=") + elapsed_time_millis + " ";
  str += string("cells_scanned=") + cells_scanned + " ";
  str += string("cells_returned=") + cells_returned + " ";
  str += string("bytes_scanned=") + bytes_scanned + " ";
  str += string("bytes_returned=") + bytes_returned + " ";
  str += string("disk_read=") + disk_read + " ";
  str += string("subscanners=") + subscanners + " ";
  str += string("scanblocks=") + scanblocks + " ";
  str += string("servers=");
  bool first = true;
  for (auto & server : servers) {
    if (first)
      first = false;
    else
      str += ",";
    str += server;
  }
  str += "}";
  return str;
}
