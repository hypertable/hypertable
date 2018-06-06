/**
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
import java.util.Arrays;
import java.util.logging.Logger;

import org.junit.*;
import static org.junit.Assert.*;

public class ChecksumTest {

    static final Logger log = Logger.getLogger("org.hypertable");

    @Before public void setUp() {
    }

    @Test public void testChecksum() {

        try {
            byte [] data = new byte [ 36 ];

            // test 1
            Arrays.fill(data, (byte)0);
            data[0] = 1;
            data[1] = 36;
            data[2] = 1;
            data[8] = 48;
            data[12] = 3;
            data[16] = -123;
            data[28] = 4;
            assertTrue(Checksum.fletcher32(data, 0, 36) == 730906148);

            // test 2
            Arrays.fill(data, (byte)0);
            data[0] = 1;
            data[1] = 36;
            data[2] = 1;
            data[8] = 35;
            data[12] = 1;
            data[16] = -40;
            data[17] = -29;
            data[18] = 6;
            data[28] = 4;
            assertTrue(Checksum.fletcher32(data, 0, 36) == -630191864);

        }
        catch (Exception e) {
            e.printStackTrace();
            assertTrue(false);
        }
    }

    @After public void tearDown() {
    }

    public static junit.framework.Test suite() {
        return new junit.framework.JUnit4TestAdapter(ChecksumTest.class);
    }

    public static void main(String args[]) {
        org.junit.runner.JUnitCore.main("org.hypertable.Checksum.ChecksumTest");
    }

}
