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

package org.hypertable.FsBroker.Lib.Response.Parameters;

import org.hypertable.Common.Serializable;

import java.io.UnsupportedEncodingException;
import java.net.ProtocolException;
import java.nio.ByteBuffer;
import java.util.zip.DataFormatException;

public class Status extends Serializable {

  /** Copy constructor.
   * @param status Status information
   */
  public Status(org.hypertable.Common.Status status) {
    synchronized (status) {
      mStatus = new org.hypertable.Common.Status(status);
    }
  }

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
   * @see encodeInternal() for encoding format
   */
  protected int encodedLengthInternal() throws UnsupportedEncodingException {
    return mStatus.encodedLength();
  }

  /** Writes serialized representation of object to a buffer.
   * This function is to be overridden by derived classes and should encode
   * the object per-se.
   * @param buf ByteBuffer destination
   */
  protected void encodeInternal(ByteBuffer buf) throws UnsupportedEncodingException {
    mStatus.encode(buf);
  }

  /** Reads serialized representation of object from a buffer.
   * This function is to be overridden by derived classes and should decode
   * the object per-se as encoded with encodeInternal().
   * @param version Encoding version
   * @param buf ByteBuffer source
   * @see encodeInternal() for encoding format
   */
  protected void decodeInternal(byte version, ByteBuffer buf) throws ProtocolException, DataFormatException, UnsupportedEncodingException {
    mStatus.decode(buf);
  }

  /** Status information */
  private org.hypertable.Common.Status mStatus;

}
