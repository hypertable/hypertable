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
import java.lang.System;
import java.nio.ByteBuffer;
import java.util.zip.DataFormatException;

public class Serialization {

  /**
   * Computes the encoded length of a byte array
   *
   * @param len length of the byte array to be encoded
   * @return the encoded length of a byte array of length len
   */
  public static int EncodedLengthByteArray(int len) {
    return len + 4;
  }


  /**
   * Encodes a variable sized byte array into the given buffer.  Encoded as a
   * 4 byte length followed by the data.  Assumes there is enough space
   * available.
   *
   * @param buf byte buffer to write encoded array to
   * @param data byte array to encode
   * @param len number of bytes of data to encode
   */
  public static void EncodeByteArray(ByteBuffer buf, byte [] data, int len) {
    buf.putInt(len);
    buf.put(data, 0, len);
  }


  /**
   * Decodes a variable sized byte array from the given buffer.  Byte array is
   * encoded as a 4 byte length followed by the data.
   *
   * @param buf byte buffer holding encoded byte array
   * @return decoded byte array, or null on error
   */
  public static byte [] DecodeByteArray(ByteBuffer buf) {
    byte [] rbytes;
    int len;
    if (buf.remaining() < 4)
      return null;
    len = buf.getInt();
    rbytes = new byte [ len ];
    if (len > 0) {
      buf.get(rbytes, 0, len);
    }
    return rbytes;
  }

  /**
   * Computes the encoded length of a c-style null-terminated string
   *
   * @param str string to encode
   * @return the encoded length of str
   */
  public static int EncodedLengthString(String str) {
    if (str == null)
      return 3;
    try {
      return 3 + str.getBytes("UTF-8").length;
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }
    return 0;
  }


  /**
   * Encodes a string into the given buffer.  Encoded as a 2 byte length
   * followed by the UTF-8 string data, followed by a '\0' termination byte.
   * The length value does not include the '\0'.  Assumes there is enough
   * space available.
   *
   * @param buf destination byte buffer to hold encoded string
   * @param str string to encode
   */
  public static void EncodeString(ByteBuffer buf, String str) {
    try {
      short len = (str == null) ? 0 : (short)str.getBytes("UTF-8").length;

      // 2-byte length
      buf.putShort(len);

      // string characters
      if (len > 0)
        buf.put(str.getBytes("UTF-8"));

      // '\0' terminator
      buf.put((byte)0);
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }
  }


  /**
   * Decodes a string from the given buffer.  The encoding of the
   * string is a 2 byte length followed by the string, followed by a '\0'
   * termination byte.  The length does not include the '\0' terminator.
   *
   * @param buf the source byte buffer holding the encoded string
   * @return decoded string on success, null otherwise
   */
  public static String DecodeString(ByteBuffer buf) {
    byte terminator;
    if (buf.remaining() < 3)
      return null;
    short len = buf.getShort();
    if (len == 0) {
      terminator = buf.get();
      assert terminator == (byte)0;
      return new String("");
    }
    byte [] sbytes = new byte [ len ];
    buf.get(sbytes);
    terminator = buf.get();
    assert terminator == (byte)0;
    try {
      return new String(sbytes, "UTF-8");
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }
    return null;
  }

  private static final long HT_MAX_V1B = 0x7FL;
  private static final long HT_MAX_V2B = 0x3FFFL;
  private static final long HT_MAX_V3B = 0x1FFFFFL;
  private static final long HT_MAX_V4B = 0xFFFFFFFL;
  private static final long HT_MAX_V5B = 0x7FFFFFFFFL;
  private static final long HT_MAX_V6B = 0x3FFFFFFFFFFL;
  private static final long HT_MAX_V7B = 0x1FFFFFFFFFFFFL;
  private static final long HT_MAX_V8B = 0xFFFFFFFFFFFFFFL;
  private static final long HT_MAX_V9B = 0x7FFFFFFFFFFFFFFFL;

  private static final int HT_MAX_LEN_VINT32 = 5;
  private static final int HT_MAX_LEN_VINT64 = 10;

  /** Returns variable-byte encoding length of an integer.
   * @param val Integer for which to get encoded length
   * @return Encoded length of <code>val</code>
   */
  public static int EncodedLengthVInt32(int val) {
    assert val >= 0;
    return  (val <= HT_MAX_V1B ? 1 :
             (val <= HT_MAX_V2B ? 2 :
              (val <= HT_MAX_V3B ? 3 :
               (val <= HT_MAX_V4B ? 4 : 5))));
  }


  /** Encodes a 32-bit integer using variable-byte encoding.
   * @param buf destination byte buffer to hold encoded string
   * @param val Integer to encode
   */
  public static void EncodeVInt32(ByteBuffer buf, int val) {
    assert val >= 0;
    if (val == 0) {
      buf.put((byte)0);
      return;
    }
    while (val > 0) {
      byte next = (byte)(val & 0x7F);
      if (val <= HT_MAX_V1B) {
        buf.put(next);
        return;
      }
      buf.put((byte)(next | 0x80));
      val >>= 7;
    }
  }


  /** Decodes a variable-byte 32-bit integer.
   * @param buf the source byte buffer holding the encoded integer
   * @return Decoded integer
   */
  public static int DecodeVInt32(ByteBuffer buf) throws DataFormatException {
    int val = 0;
    byte next = buf.get();
    int shift = 0;
    int count = 1;

    while (next < 0) {
      if (count == HT_MAX_LEN_VINT32)
        throw new DataFormatException("Too many bytes in encoded vint32");
      val = val | (((int)next & 0x7f) << shift);
      shift += 7;
      next = buf.get();
      count++;
    }
    return val | ((int)next << shift);
  }

  /** Returns variable-byte encoding length of a long integer.
   * @param val Long integer for which to get encoded length
   * @return Encoded length of <code>val</code>
   */
  public static int EncodedLengthVInt64(long val) {
    assert val >= 0;
    return (val <= HT_MAX_V1B ? 1 :
            (val <= HT_MAX_V2B ? 2 :
             (val <= HT_MAX_V3B ? 3 :
              (val <= HT_MAX_V4B ? 4 :
               (val <= HT_MAX_V5B ? 5 :
                (val <= HT_MAX_V6B ? 6 :
                 (val <= HT_MAX_V7B ? 7 :
                  (val <= HT_MAX_V8B ? 8 :
                   (val <= HT_MAX_V9B ? 9 : 10)))))))));
  }


  /** Encodes a 64-bit long using variable-byte encoding.
   * @param buf destination byte buffer to hold encoded long
   * @param val Long integer to encode
   */
  public static void EncodeVInt64(ByteBuffer buf, long val) {
    assert val >= 0;
    if (val == 0) {
      buf.put((byte)0);
      return;
    }
    while (val > 0) {
      byte next = (byte)(val & 0x7F);
      if (val <= HT_MAX_V1B) {
        buf.put(next);
        return;
      }
      buf.put((byte)(next | 0x80));
      val >>= 7;
    }
  }


  /** Decodes a variable-byte 64-bit integer.
   * @param buf the source byte buffer holding the encoded string
   * @return Decoded integer
   */
  public static long DecodeVInt64(ByteBuffer buf) throws DataFormatException {
    long val = 0;
    byte next = buf.get();
    int shift = 0;
    int count = 1;

    while (next < 0) {
      if (count == HT_MAX_LEN_VINT64)
        throw new DataFormatException("Too many bytes in encoded vint64");
      val = val | (((long)next & 0x7f) << shift);
      shift += 7;
      next = buf.get();
      count++;
    }
    return val | ((long)next << shift);
  }

  /** Returns length of encoded string in vstr format.
   * @param str String for which to get encoded length
   * @return Encoded length of <code>str</code>
   */
  public static int EncodedLengthVStr(String str) throws UnsupportedEncodingException {
    if (str == null)
      return EncodedLengthVInt64(0) + 1;
    return EncodedLengthVInt64(str.getBytes("UTF-8").length) + str.getBytes("UTF-8").length + 1;
  }


  /** Encodes a string in vstr format.
   * @param buf Destination byte buffer to hold encoded string
   * @param str String to encode
   */
  public static void EncodeVStr(ByteBuffer buf, String str)
    throws UnsupportedEncodingException {

    long len = (str == null) ? 0 : (long)str.getBytes("UTF-8").length;

    // vint64 length
    EncodeVInt64(buf, len);

    // string characters
    if (len > 0)
      buf.put(str.getBytes("UTF-8"));

    // '\0' terminator
    buf.put((byte)0);

  }


  /** Decodes a string in vstr format.
   * @param buf the source byte buffer holding the encoded string
   * @return Decoded string
   */
  public static String DecodeVStr(ByteBuffer buf)
    throws DataFormatException, UnsupportedEncodingException {
    int len = DecodeVInt32(buf);
    if (len == 0) {
      buf.get(); // skip \0 terminator
      return new String("");
    }
    byte [] sbytes = new byte [ len ];
    buf.get(sbytes);
    buf.get();  // skip '\0' terminator
    return new String(sbytes, "UTF-8");
  }


}
