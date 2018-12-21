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

package org.hypertable.Common;

import java.io.UnsupportedEncodingException;
import java.net.ProtocolException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.zip.DataFormatException;

public class Filesystem {

  /** Enumeration for flags */
  public enum Flags {
    NONE(0), FLUSH(1), SYNC(2);
    private final int value;
    private Flags(int value) {
      this.value = value;
    }
    public int getValue() {
      return value;
    }
    private static Flags[] allValues = values();
    public static Flags fromOrdinal(int n) {return allValues[n];}
  };

  /**
   * A directory entry for readdir.
   */
  public static class Dirent {
    static final byte DIRENT_VERSION = 1;
    public int EncodedLength() throws UnsupportedEncodingException {
      int length = 13 + Serialization.EncodedLengthVStr(name);
      return 1 + Serialization.EncodedLengthVInt32(length) + length;
    }
    public void Encode(ByteBuffer buf) throws UnsupportedEncodingException {
      buf.order(ByteOrder.LITTLE_ENDIAN);
      buf.put(DIRENT_VERSION);
      int encoding_length = 13 + Serialization.EncodedLengthVStr(name);
      Serialization.EncodeVInt32(buf, encoding_length);
      Serialization.EncodeVStr(buf, name);
      buf.putLong(length);
      buf.putInt(last_modification_time);
      buf.put(is_dir ? (byte)1 : (byte)0);
    }
    public void Decode(ByteBuffer buf)
      throws ProtocolException, UnsupportedEncodingException, DataFormatException {
      buf.order(ByteOrder.LITTLE_ENDIAN);
      if (buf.remaining() < 2)
        throw new ProtocolException("Truncated message");
      int version = (int)buf.get();
      if (version != DIRENT_VERSION)
        throw new ProtocolException("Dirent parameters version mismatch, expected " +
                                    DIRENT_VERSION + ", got " + version);
      int encoding_length = Serialization.DecodeVInt32(buf);
      int start_position = buf.position();
      name = Serialization.DecodeVStr(buf);
      length = buf.getLong();
      last_modification_time = buf.getInt();
      is_dir = buf.get() == (byte)0 ? false : true;
      if ((buf.position() - start_position) < encoding_length)
        buf.position(start_position + encoding_length);
    }
    public String name;
    public long length = 0;
    public int last_modification_time = 0;
    public boolean is_dir = false;
  }

}
