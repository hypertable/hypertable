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

public class Status extends Serializable {

  /** Enumeration for status codes. */
  public enum Code {
    OK(0), WARNING(1), CRITICAL(2), UNKNOWN(3);
    private final int value;
    private Code(int value) {
      this.value = value;
    }
    public int getValue() {
      return value;
    }
  };

  /// Status text string constants.
  public class Text {
    static public final String SERVER_IS_COMING_UP = "Server is coming up";
    static public final String SERVER_IS_SHUTTING_DOWN = "Server is shutting down";
    static public final String STANDBY = "Standby";
  };

  /** Constructor.
   */
  public Status() { }

  /** Copy constructor.
   * @param other Other status object to make copy from
   */
  public Status(Status other) {
    synchronized (other) {
      mCode = other.mCode;
      mText = other.mText;
    }
  }


  /** Sets status code and text
   * @param code %Status code
   * @param text %Status text
   */
  public synchronized void set(Code code, String text) {
    mCode = code;
    mText = text;
  }

  /** Gets status code.
   * @return Status code
   */
  public synchronized Code get() { return mCode; }

  /** Returns encoding version.
   * @return Encoding version
   */
  protected byte encodingVersion() {
    return 1;
  }

  /** Returns internal serialized length.
   * This function is to be overridden by derived classes and should return
   * the length of the the serialized object per-se.
   * @return Internal serialized length
   * @see #encodeInternal(ByteBuffer) encodeInternal
   */
  protected int encodedLengthInternal() throws UnsupportedEncodingException {
    return 4 + Serialization.EncodedLengthVStr(mText);
  }

  /** Writes serialized representation of object to a buffer.
   * This function is to be overridden by derived classes and should encode
   * the object per-se.
   * @param buf ByteBuffer destination
   */
  protected void encodeInternal(ByteBuffer buf) throws UnsupportedEncodingException {
    buf.putInt(mCode.getValue());
    Serialization.EncodeVStr(buf, mText);
  }

  /** Reads serialized representation of object from a buffer.
   * This function is to be overridden by derived classes and should decode
   * the object per-se as encoded with encodeInternal().
   * @param version Encoding version
   * @param buf ByteBuffer source
   * @see #encodeInternal(ByteBuffer) encodeInternal
   */
  protected void decodeInternal(byte version, ByteBuffer buf) throws ProtocolException, DataFormatException, UnsupportedEncodingException {
    mCode = Code.values()[buf.getInt()];
    mText = Serialization.DecodeVStr(buf);
  }

  /** Status code */
  private Code mCode = Code.OK;

  /** Status text */
  private String mText;


}
