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
import java.util.zip.DataFormatException;

public abstract class Serializable {

  /** Returns serialized object length.
   * Returns the serialized length of the object as encoded by encode().
   * @see #encode(ByteBuffer) encode
   * @return Overall serialized object length
   */
  public int encodedLength() throws UnsupportedEncodingException {
    int length = encodedLengthInternal();
    return 1 + Serialization.EncodedLengthVInt32(length) + length;
  }

  /** Writes serialized representation of object to a buffer.
   * This function encodes a serialized representation of the object,
   * starting with a header that includes the encoding version and the
   * serialized length of the object.  After the header, the object per-se is
   * encoded with encodeInternal().
   * @param buf ByteBuffer destination
   */
  public void encode(ByteBuffer buf) throws UnsupportedEncodingException {
    buf.put(encodingVersion());
    Serialization.EncodeVInt32(buf, encodedLengthInternal());
    encodeInternal(buf);
  }

  /** Reads serialized representation of object from a buffer.
   * @param buf ByteBuffer source
   * @see #encode(ByteBuffer) encode
   * @throws ProtocolException
   */
  public void decode(ByteBuffer buf) throws ProtocolException, DataFormatException, UnsupportedEncodingException {
    byte version = buf.get();
    if (version > encodingVersion())
      throw new ProtocolException("Unsupported encoding version (" + version + ")");
    int encoding_length = Serialization.DecodeVInt32(buf);
    ByteBuffer sub = buf.duplicate();
    sub.limit(buf.position() + encoding_length);
    decodeInternal(version, sub);
    buf.position(buf.position() + encoding_length);
  }

  /** Returns encoding version.
   * @return Encoding version
   */
  abstract protected byte encodingVersion();

  /** Returns internal serialized length.
   * This function is to be overridden by derived classes and should return
   * the length of the the serialized object per-se.
   * @return Internal serialized length
   * @see #encodeInternal(ByteBuffer) encodeInternal
   */
  abstract protected int encodedLengthInternal() throws UnsupportedEncodingException;

  /** Writes serialized representation of object to a buffer.
   * This function is to be overridden by derived classes and should encode
   * the object per-se.
   * @param buf ByteBuffer destination
   */
  abstract protected void encodeInternal(ByteBuffer buf) throws UnsupportedEncodingException;

  /** Reads serialized representation of object from a buffer.
   * This function is to be overridden by derived classes and should decode
   * the object per-se as encoded with encodeInternal().
   * @param version Encoding version
   * @param buf ByteBuffer source
   * @see #encodeInternal(ByteBuffer) encodeInternal
   */
  abstract protected void decodeInternal(byte version, ByteBuffer buf) throws ProtocolException, DataFormatException, UnsupportedEncodingException;


}
