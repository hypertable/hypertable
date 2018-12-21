/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
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

package org.hypertable.FsBroker.Lib;

import org.hypertable.AsyncComm.ResponseCallback;

import org.hypertable.Common.Filesystem;

public interface Broker {

  static final int OPEN_FLAG_DIRECT          = 0x00000001;
  static final int OPEN_FLAG_OVERWRITE       = 0x00000002;
  static final int OPEN_FLAG_VERIFY_CHECKSUM = 0x00000004;

  OpenFileMap GetOpenFileMap();

  void Open(ResponseCallbackOpen cb, String fileName, int flags, int bufferSize);

  void Close(ResponseCallback cb, int fd);

  void Create(ResponseCallbackCreate cb, String fileName, int flags,
              int bufferSize, short replication, long blockSize);
  
  void Length(ResponseCallbackLength cb, String fileName, boolean accurate);

  void Mkdirs(ResponseCallback cb, String fileName);

  void Read(ResponseCallbackRead cb, int fd, int amount);

  void Append(ResponseCallbackAppend cb, int fd, int amount, byte [] data,
              Filesystem.Flags flags);

  void PositionRead(ResponseCallbackPositionRead cb, int fd, long offset,
                    int amount, boolean verify_checksum);

  void Remove(ResponseCallback cb, String fileName);

  void Seek(ResponseCallback cb, int fd, long offset);

  void Flush(ResponseCallback cb, int fd);

  void Sync(ResponseCallback cb, int fd);

  void Rmdir(ResponseCallback cb, String fileName);

  void Readdir(ResponseCallbackReaddir cb, String dirName);

  void Exists(ResponseCallbackExists cb, String fileName);

  void Rename(ResponseCallback cb, String src, String dst);

  void Status(ResponseCallbackStatus cb);

  void Debug(ResponseCallback cb, int command, byte [] parmas);

  void Shutdown();

}
