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

import org.hypertable.AsyncComm.CommBuf;
import org.hypertable.AsyncComm.CommHeader;

public class Message {

  public enum Type { REGISTER, SETUP, READY, TASK, FINISHED, SUMMARY, ERROR }

  public Message(Type t) {
    mType = t;
  }

  public CommBuf createCommBuf(CommHeader header) {
    CommBuf cbuf = new CommBuf(header, 1+this.encodedLength());
    cbuf.AppendByte((byte)this.type().ordinal());
    this.encode(cbuf.data);
    return cbuf;
  }

  public String toString() {
    if (mType == Type.REGISTER)
      return new String("MESSAGE:REGISTER");
    else if (mType == Type.SETUP)
      return new String("MESSAGE:SETUP");
    else if (mType == Type.READY)
      return new String("MESSAGE:READY");
    else if (mType == Type.TASK)
      return new String("MESSAGE:TASK");
    else if (mType == Type.FINISHED)
      return new String("MESSAGE:FINISHED");
    else if (mType == Type.SUMMARY)
      return new String("MESSAGE:SUMMARY");
    return null;
  }

  public int encodedLength() { return 0; }
  public void encode(ByteBuffer buf) {  }
  public void decode(ByteBuffer buf) {  }

  public Type type() { return mType; }

  protected Type mType;
}
