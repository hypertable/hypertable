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
import java.lang.NumberFormatException;
import java.lang.System;
import java.net.InetSocketAddress;
import java.text.ParseException;
import java.util.Arrays;
import java.util.Vector;
import java.util.logging.Logger;

import org.junit.*;
import static org.junit.Assert.*;

public class HostSpecificationTest {

  static final Logger log = Logger.getLogger("org.hypertable");

  static private Vector<String> valid;
  static private Vector<String> invalid;
  static private Vector<String> valid_expanded;

    @Before public void setUp() {

      valid = new Vector<String>(11);
      valid.add("test1 test2 test[1-2]a test[1-2]b");
      valid.add("host[01-36] - host[20-35]");
      valid.add("(host[01-36].hypertable.com - host17.hypertable.com) - host[20-35].hypertable.com");
      valid.add("(host[01-15].hypertable.com) + (host[20-35].hypertable.com - host29.hypertable.com)");
      valid.add("host[1-17]");
      valid.add("host[7-9] + host10");
      valid.add("host[7-9]\nhost10");
      valid.add("host1, host2, host3");
      valid.add("host[01-10] - host07");
      valid.add("host[01-10] -host07");
      valid.add("ht-rel-css-slave[21-25]");

      valid_expanded = new Vector<String>(11);
      valid_expanded.add("test1 test1a test1b test2 test2a test2b ");
      valid_expanded.add("host01 host02 host03 host04 host05 host06 host07 host08 host09 host10 host11 host12 host13 host14 host15 host16 host17 host18 host19 host36 ");
      valid_expanded.add("host01.hypertable.com host02.hypertable.com host03.hypertable.com host04.hypertable.com host05.hypertable.com host06.hypertable.com host07.hypertable.com host08.hypertable.com host09.hypertable.com host10.hypertable.com host11.hypertable.com host12.hypertable.com host13.hypertable.com host14.hypertable.com host15.hypertable.com host16.hypertable.com host18.hypertable.com host19.hypertable.com host36.hypertable.com ");
      valid_expanded.add("host01.hypertable.com host02.hypertable.com host03.hypertable.com host04.hypertable.com host05.hypertable.com host06.hypertable.com host07.hypertable.com host08.hypertable.com host09.hypertable.com host10.hypertable.com host11.hypertable.com host12.hypertable.com host13.hypertable.com host14.hypertable.com host15.hypertable.com host20.hypertable.com host21.hypertable.com host22.hypertable.com host23.hypertable.com host24.hypertable.com host25.hypertable.com host26.hypertable.com host27.hypertable.com host28.hypertable.com host30.hypertable.com host31.hypertable.com host32.hypertable.com host33.hypertable.com host34.hypertable.com host35.hypertable.com ");
      valid_expanded.add("host1 host2 host3 host4 host5 host6 host7 host8 host9 host10 host11 host12 host13 host14 host15 host16 host17 ");
      valid_expanded.add("host7 host8 host9 host10 ");
      valid_expanded.add("host7 host8 host9 host10 ");
      valid_expanded.add("host1 host2 host3 ");
      valid_expanded.add("host01 host02 host03 host04 host05 host06 host08 host09 host10 ");
      valid_expanded.add("host01 host02 host03 host04 host05 host06 host08 host09 host10 ");
      valid_expanded.add("ht-rel-css-slave21 ht-rel-css-slave22 ht-rel-css-slave23 ht-rel-css-slave24 ht-rel-css-slave25 ");

      invalid = new Vector<String>(19);
      invalid.add("test1 test~2 test3");
      invalid.add("test[a-07]");
      invalid.add("test[0a-07]");
      invalid.add("test[08]");
      invalid.add("test[08-a]");
      invalid.add("test[08-020]");
      invalid.add("test[");
      invalid.add("test[08");
      invalid.add("test[08-");
      invalid.add("test[08-10");
      invalid.add("test[0	8-10]");
      invalid.add("- host[01-30]");
      invalid.add("+ host[01-30]");
      invalid.add("(- host[01-30])");
      invalid.add("(+ host[01-30])");
      invalid.add("host[01-30] -");
      invalid.add("host[01-30] +");
      invalid.add("(host[01-30] -)");
      invalid.add("(host[01-30] +)");
    }

    @Test public void testHostSpecification() {

        try {

          for (int i=0; i<valid.size(); i++) {

            HostSpecification hs = new HostSpecification(valid.elementAt(i));
            Vector<String> expansion = hs.expand();

            String expanded = new String();
            for (String host : expansion)
              expanded += host + " ";
            System.out.println("# " + valid.elementAt(i));
            System.out.println(expanded);
            assertTrue(expanded.equals(valid_expanded.elementAt(i)));
          }
          
        }
        catch (Exception e) {
            e.printStackTrace();
            assertTrue(false);
        }

        for (String spec : invalid) {
          try {
            HostSpecification hs = new HostSpecification(spec);
            Vector<String> expansion = hs.expand();
            assertTrue(false);
          }
          catch (ParseException e) {
            System.out.println("# " + spec);
            System.out.println(e.getMessage() + " at " + e.getErrorOffset());
          }
          catch (NumberFormatException e) {
            System.out.println("# " + spec);
            System.out.println(e.getMessage());
          }
        }

    }

    @After public void tearDown() {
    }

    public static junit.framework.Test suite() {
      return new junit.framework.JUnit4TestAdapter(HostSpecificationTest.class);
    }

    public static void main(String args[]) {
      org.junit.runner.JUnitCore.main("org.hypertable.Common.HostSpecificationTest");
    }

}
