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

public class ColumnNameSplit {
  /**
   * Returns true if the column String contains this qualified column
   * Assumes column string contains 1 byte wide chars
   * @param column comlumn name
   * @param columnFamily return byte array which will contain the columnFamily component
   * @param columnQualifier return byte array which contains the columnQualifier component
   * @return true if the column contains this qualified column
   */
  public static boolean split(String column, byte [] columnFamily, byte [] columnQualifier) {
    int cfLength, cqOffset, cqLength;
    int splitPos = column.indexOf(':');
    boolean hasCf = false;

    if (splitPos == -1) {
      columnQualifier = null;
      cfLength = column.length();
      columnFamily = new byte[cfLength];
      java.lang.System.arraycopy(column.getBytes(), 0, columnFamily, 0, cfLength);
      return false;
    }
    else {
      hasCf = true;
      cfLength = splitPos;
      cqOffset = splitPos + 1;
      cqLength = column.length() - splitPos - 1;
      columnFamily = new byte [cfLength];
      columnQualifier = new byte [cqLength];
      java.lang.System.arraycopy(column.getBytes(), 0, columnFamily, 0, cfLength);
      java.lang.System.arraycopy(column.getBytes(), cqOffset, columnQualifier, 0, cqLength);
    }
    return hasCf;
  }

}
