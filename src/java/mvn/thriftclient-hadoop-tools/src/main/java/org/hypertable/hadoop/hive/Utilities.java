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

package org.hypertable.hadoop.hive;

public class Utilities {

  static String getTableName(String hypertableTableName) {
    int lastslash = hypertableTableName.lastIndexOf('/');
    if (lastslash == -1)
      return hypertableTableName;
    else
      return hypertableTableName.substring(lastslash+1);
  }

  static String getNamespace(String hypertableTableName) {
    int lastslash = hypertableTableName.lastIndexOf('/');
    if (lastslash > 0) {
      if (hypertableTableName.startsWith("/"))
        return hypertableTableName.substring(0, lastslash);
      else
        return "/"+hypertableTableName.substring(0, lastslash);
    }
    return "/";
  }

}
