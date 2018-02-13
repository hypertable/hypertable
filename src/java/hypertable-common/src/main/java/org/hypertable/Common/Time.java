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

import java.text.ParseException;
import java.util.Date;
import java.util.GregorianCalendar;

public class Time {

  public static Date parse_ts(String time_string) throws ParseException {
    int year = 0;
    int month = 0;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int offset = 0;
    GregorianCalendar cal = new GregorianCalendar();

    /** year **/
    offset = time_string.indexOf('-', offset);
    if (offset == -1) {
      if (time_string.length() != 4)
        throw new ParseException("Unparseable date: " + time_string, 0);
      year = Integer.parseInt(time_string);
      cal.set(year, month, day, hour, minute, second);
      return cal.getTime();
    }
    else if (offset != 4) 
      throw new ParseException("Unparseable date: " + time_string, 0);
    else {
      year = Integer.parseInt(time_string.substring(0, 4));
      offset = 5;
    }

    /** month **/
    offset = time_string.indexOf('-', offset);
    if (offset == -1) {
      if (time_string.length() != 7)
        throw new ParseException("Unparseable date: " + time_string, 5);
      month = Integer.parseInt(time_string.substring(5));
      if (month == 0)
        throw new ParseException("Unparseable date: " + time_string, 5);
      month--;
      cal.set(year, month, day, hour, minute, second);
      return cal.getTime();
    }
    else if (offset != 7)
      throw new ParseException("Unparseable date: " + offset + " foo " + time_string, 5);
    else {
      month = Integer.parseInt(time_string.substring(5, 7));
      if (month == 0)
        throw new ParseException("Unparseable date: " + time_string, 5);
      month--;
      offset = 8;
    }

    /** day **/
    offset = time_string.indexOf(' ', offset);
    if (offset == -1) {
      if (time_string.length() != 10)
        throw new ParseException("Unparseable date: " + time_string, 8);
      day = Integer.parseInt(time_string.substring(8));
      cal.set(year, month, day, hour, minute, second);
      return cal.getTime();
    }
    else if (offset != 10)
      throw new ParseException("Unparseable date: " + time_string, 8);
    else {
      day = Integer.parseInt(time_string.substring(8, 10));
      offset = 11;
    }

    /** hour **/
    offset = time_string.indexOf(':', offset);
    if (offset == -1) {
      if (time_string.length() != 13)
        throw new ParseException("Unparseable date: " + time_string, 11);
      hour = Integer.parseInt(time_string.substring(11));
      cal.set(year, month, day, hour, minute, second);
      return cal.getTime();
    }
    else if (offset != 13)
      throw new ParseException("Unparseable date: " + time_string, 11);
    else {
      hour = Integer.parseInt(time_string.substring(11, 13));
      offset = 14;
    }

    /** minute **/
    offset = time_string.indexOf(':', offset);
    if (offset == -1) {
      if (time_string.length() != 16)
        throw new ParseException("Unparseable date: " + time_string, 14);
      minute = Integer.parseInt(time_string.substring(14));
      cal.set(year, month, day, hour, minute, second);
      return cal.getTime();
    }
    else if (offset != 16)
      throw new ParseException("Unparseable date: " + time_string, 14);
    else {
      minute = Integer.parseInt(time_string.substring(14, 16));
      offset = 17;
    }

    /** second **/
    offset = time_string.indexOf(':', offset);
    if (offset == -1) {
      if (time_string.length() != 19)
        throw new ParseException("Unparseable date: " + time_string, 17);
      second = Integer.parseInt(time_string.substring(17));
      cal.set(year, month, day, hour, minute, second);
      return cal.getTime();
    }
    else if (offset != 19)
      throw new ParseException("Unparseable date: " + time_string, 17);
    else {
      second = Integer.parseInt(time_string.substring(17, 13));
    }
    
    cal.set(year, month, day, hour, minute, second);
    return cal.getTime();
  }

}