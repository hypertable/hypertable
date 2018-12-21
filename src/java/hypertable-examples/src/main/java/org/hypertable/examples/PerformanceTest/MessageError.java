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

package org.hypertable.examples.PerformanceTest;

import java.nio.ByteBuffer;

import org.hypertable.Common.Serialization;

public class MessageError extends Message {

  public MessageError() {
    super(Message.Type.ERROR);
  }

  public MessageError(String msg) {
    super(Message.Type.ERROR);
    mMessage = msg;
  }

  public int encodedLength() {
    return Serialization.EncodedLengthString(mMessage);
  }
  public void encode(ByteBuffer buf) {
    Serialization.EncodeString(buf, mMessage);
  }
  public void decode(ByteBuffer buf) {
    mMessage = Serialization.DecodeString(buf);
  }

  public String toString() {
    return new String("MESSAGE:ERROR { " + mMessage + "}");
  }

  public void setMessage(String msg) { mMessage = msg; }
  public String getMessage() { return mMessage; }

  private String mMessage;
}
