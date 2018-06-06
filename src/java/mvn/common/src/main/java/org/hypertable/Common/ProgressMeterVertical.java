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

import java.lang.System;

public class ProgressMeterVertical {

  public ProgressMeterVertical(long expectedCount) {
    mExpectedCount = expectedCount;
    System.out.println("  0% complete.");
    System.out.flush();
  }

  //  Effects: Display appropriate progress tic if needed.
  //  Postconditions: count()== original count() + increment
  //  Returns: count().
  public long add( long increment ) {
    mCount += increment;
    int pct = (int)((mCount*100) / mExpectedCount);
    if (pct > mLastPercentage) {
      long elapsed_secs = (System.currentTimeMillis()-mStartTime)/1000;
      if (pct < 10)
        System.out.println("  " + pct + "% complete\t" + elapsed_secs);
      else if (pct < 100)
        System.out.println(" " + pct + "% complete\t" + elapsed_secs);
      else
        System.out.println(pct + "% complete\t" + elapsed_secs);
      System.out.flush();
      mLastPercentage = pct;
    }
    return mCount;
  }

  public void finished() {
    if (mLastPercentage < 100) {
      System.out.println("100% complete.");
      System.out.flush();
      mLastPercentage = 100;
    }
  }

  private long mStartTime = System.currentTimeMillis();
  private long mCount;
  private long mExpectedCount;
  private int  mLastPercentage;
}

