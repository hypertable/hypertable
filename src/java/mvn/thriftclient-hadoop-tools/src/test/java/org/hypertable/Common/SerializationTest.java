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

import java.io.File;
import java.io.InputStream;
import java.lang.System;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.logging.Logger;

import org.junit.*;
import static org.junit.Assert.*;

public class SerializationTest {

  static final Logger log = Logger.getLogger("org.hypertable");

  @Before public void setUp() {
  }

  @Test public void testChecksum() {

    try {

      /**
       * vint32
       */

      int[] testValues = { 0x000000000, 0x00000007F, 0x000000080, 0x000003FFF,
                           0x000004000, 0x0001FFFFF, 0x000200000, 0x00FFFFFFF,
                           0x010000000, 0x07FFFFFFF };
      int[] encodedLengths = { 1, 1, 2, 2, 3, 3, 4, 4, 5, 5 };

      ByteBuffer buf = ByteBuffer.allocate(32);
      int result;

      for (int i=0; i<testValues.length; i++) {
        buf.clear();
        Serialization.EncodeVInt32(buf, testValues[i]);
        buf.flip();
        result = Serialization.DecodeVInt32(buf);
        assertTrue(result == testValues[i]);
        assertTrue(Serialization.EncodedLengthVInt32(testValues[i]) == encodedLengths[i]);
      }

      /**
       * vint64
       */

      long long_val = 0;
      buf.clear();
      Serialization.EncodeVInt64(buf, long_val);
      buf.flip();
      assertTrue(Serialization.DecodeVInt64(buf) == long_val);

      for (int i=0; i<62; i++) {
        long_val = (long_val << 1) | 1;
        buf.clear();
        Serialization.EncodeVInt64(buf, long_val);
        buf.flip();
        assertTrue(Serialization.DecodeVInt64(buf) == long_val);
      }

      /**
       * vint64
       */

      buf.clear();
      Serialization.EncodeVStr(buf, "Hello, World!");
      buf.flip();
      String after = Serialization.DecodeVStr(buf);
      assertTrue(after.equals("Hello, World!"));

      buf.clear();
      Serialization.EncodeVStr(buf, null);
      buf.flip();
      after = Serialization.DecodeVStr(buf);
      assertTrue(after.equals(""));

    }
    catch (Exception e) {
      e.printStackTrace();
      assertTrue(false);
    }
  }

  @After public void tearDown() {
  }

  public static junit.framework.Test suite() {
    return new junit.framework.JUnit4TestAdapter(SerializationTest.class);
  }

  public static void main(String args[]) {
    org.junit.runner.JUnitCore.main("org.hypertable.Checksum.SerializationTest");
  }

}
