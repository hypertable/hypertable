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
import java.nio.ByteOrder;

import org.hypertable.Common.Serialization;

public class Setup {

  public enum Type { READ, WRITE, SCAN, INCR }

  public enum Order { SEQUENTIAL, RANDOM }

  public enum Distribution { UNIFORM, ZIPFIAN }

  public Setup() { }

  public int encodedLength() { return 52 + Serialization.EncodedLengthString(tableName) + 
      Serialization.EncodedLengthString(driver) + Serialization.EncodedLengthString(valueData) +
      Serialization.EncodedLengthString(cmfFile); }

  public void encode(ByteBuffer buf) {
    buf.putInt(type.ordinal());
    buf.putLong(keyMax);
    buf.putInt(keySize);
    buf.putInt(valueSize);
    buf.putLong(keyCount);
    buf.putInt(order.ordinal());
    buf.putInt(distribution.ordinal());
    buf.putLong(distributionRange);
    buf.putInt(scanBufferSize);
    buf.putInt(parallelism);
    Serialization.EncodeString(buf, tableName);
    Serialization.EncodeString(buf, driver);
    Serialization.EncodeString(buf, valueData);
    Serialization.EncodeString(buf, cmfFile);
  }

  public void decode(ByteBuffer buf) {
    type  = Type.values()[buf.getInt()];
    keyMax = buf.getLong();
    keySize = buf.getInt();
    valueSize = buf.getInt();
    keyCount = buf.getLong();
    order = Order.values()[buf.getInt()];
    distribution = Distribution.values()[buf.getInt()];
    distributionRange = buf.getLong();
    scanBufferSize = buf.getInt();
    parallelism = buf.getInt();
    tableName = Serialization.DecodeString(buf);
    driver = Serialization.DecodeString(buf);
    valueData = Serialization.DecodeString(buf);
    cmfFile = Serialization.DecodeString(buf);
  }

  public String toString() {
    return new String("type=" + type + 
                      ", keyMax=" + keyMax +
                      ", keySize=" + keySize +
                      ", valueSize=" + valueSize +
                      ", keyCount=" + keyCount +
                      ", order=" + order + ", distribution=" + distribution +
		      ", distributionRange=" + distributionRange +
                      ", scanBufferSize=" + scanBufferSize +
                      ", parallelism=" + parallelism +
                      ", tableName=" + tableName +
                      ", driver=" + driver +
                      ", valueData=" + valueData +
                      ", cmfFile=" + cmfFile +
		      ")");
  }

  public Type type = Type.READ;
  public Order order = Order.SEQUENTIAL;
  public Distribution distribution = Distribution.UNIFORM;
  public long distributionRange = 0;
  public long keyCount = -1;
  public long keyMax = -1;
  public int  keySize = -1;
  public int  valueSize = -1;
  public int  scanBufferSize = 65536;
  public int parallelism = 0;
  public String tableName;
  public String driver;
  public String valueData;
  public String cmfFile;
}
