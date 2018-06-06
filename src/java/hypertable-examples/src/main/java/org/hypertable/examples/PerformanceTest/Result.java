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

public class Result {

  public int encodedLength() {
    return 48;
  }
  public void encode(ByteBuffer buf) {
    buf.putLong(itemsSubmitted);
    buf.putLong(itemsReturned);
    buf.putLong(requestCount);
    buf.putLong(valueBytesReturned);
    buf.putLong(elapsedMillis);
    buf.putLong(cumulativeLatency);
  }
  public void decode(ByteBuffer buf) {
    itemsSubmitted = buf.getLong();
    itemsReturned = buf.getLong();
    requestCount = buf.getLong();
    valueBytesReturned = buf.getLong();
    elapsedMillis = buf.getLong();
    cumulativeLatency = buf.getLong();
  }

  public String toString() {
    return new String("(items-submitted=" + itemsSubmitted + ", items-returned=" + itemsReturned +
                      "requestCount=" + requestCount + "value-bytes-returned=" + valueBytesReturned +
                      ", elapsed-millis=" + elapsedMillis + ", cumulativeLatency=" + cumulativeLatency + ")");
  }

  public long itemsSubmitted = 0;
  public long itemsReturned = 0;
  public long requestCount = 0;
  public long valueBytesReturned = 0;
  public long elapsedMillis = 0;
  public long cumulativeLatency = 0;
}
