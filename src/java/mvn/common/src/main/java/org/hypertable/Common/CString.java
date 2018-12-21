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

import java.lang.Math;

public class CString {

  public static int memcmp(byte [] a, int a_offset, int a_length,
                           byte [] b, int b_offset, int b_length) {
    int min_len = Math.min(a_length, b_length);
    for (int i=0; i<min_len; i++) {
      if (a[a_offset] != b[b_offset])
        return a[a_offset] - b[b_offset];
      a_offset++;
      b_offset++;
    }
    return a_length - b_length;
  }

}                           
