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

public class MessageSummary extends Message {

  public MessageSummary() {
    super(Message.Type.SUMMARY);
  }

  public MessageSummary(String name, Result s) {
    super(Message.Type.SUMMARY);
    mClientName = name;
    mResult = s;
  }

  public int encodedLength() {
    return Serialization.EncodedLengthString(mClientName) +
      mResult.encodedLength();
  }
  public void encode(ByteBuffer buf) {
    Serialization.EncodeString(buf, mClientName);
    mResult.encode(buf);
  }
  public void decode(ByteBuffer buf) {
    mClientName = Serialization.DecodeString(buf);
    mResult = new Result();
    mResult.decode(buf);
  }

  public String toString() {
    return new String("MESSAGE:SUMMARY { client=" + mClientName + ", result=" + mResult + " }");
  }

  public void setClientName(String name) { mClientName = name; }
  public String getClientName() { return mClientName; }

  public void setResult(Result result) { mResult = result; }
  public Result getResult() { return mResult; }

  private String mClientName;
  private Result mResult;
}
