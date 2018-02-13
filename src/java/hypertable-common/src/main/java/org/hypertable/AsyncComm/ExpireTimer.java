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

/** @file
 * Definitions for ExpireTimer.
 * This file contains the type definition for ExpireTimer, a class for holding
 * timer state.
 */

package org.hypertable.AsyncComm;

import java.lang.Comparable;

/** Encapsulates a registered timer.
 * Timer events can be registered with AsyncComm and this class is used to hold
 * the state of a registered timer.  Objects of this class are stored on a heap
 * and when a timer expires, it is removed from the heap and the application is
 * notified with a TIMER event delivered through the <code>handler</code>
 * member.
 */
public class ExpireTimer implements Comparable<ExpireTimer> {

  /** Dispatch handler invoked when timer expires. */
  public DispatchHandler handler;

  /** Expiration time of timer */
  public long expire_time;

  /** Comparison function.
   * Compares expire time.  Later (larger) expire times are considered less than
   * earlier ones.
   * @param other Other timer object with which to compare
   * @return A negative integer, zero, or a positive integer as this object is
   * less than, equal to, or greater than <code>other</code>.
   */
  public int compareTo(ExpireTimer other) {
    long diff = other.expire_time - expire_time;
    if (diff < 0)
      return -1;
    else if (diff > 0)
      return 1;
    return 0;
  }
}
