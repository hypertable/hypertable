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

public class ProgressMeter {

  public ProgressMeter(long expectedCount) {
    mExpectedCount = expectedCount;
    System.out.println("0%   10   20   30   40   50   60   70   80   90   100%");
    System.out.println("|----|----|----|----|----|----|----|----|----|----|");
    System.out.flush();
  }

  //  Effects: Display appropriate progress tic if needed.
  //  Postconditions: count()== original count() + increment
  //  Returns: count().
  public long add( long increment ) {
    if ( (mCount += increment) >= mNextTicCount ) { displayTic(); }
    return mCount;
  }

  // use of floating point ensures that both large and small counts
  // work correctly.  static_cast<>() is also used several places
  // to suppress spurious compiler warnings. 
  void displayTic() {
    int tics_needed = (int)(((double)mCount/(double)mExpectedCount) * 50.0d);

    do {
      System.out.print("*");
      System.out.flush();
    } while (++mTic < tics_needed);

    mNextTicCount = (long)((mTic/50.0)*mExpectedCount);

    if ( mCount == mExpectedCount ) {
      if ( mTic < 51 ) 
        System.out.print("*");
      System.out.println();
      System.out.flush();
      mCompleted = true;
    }
  }

  public void finished() {
    if (!mCompleted) {
      System.out.println();
      System.out.flush();
      mCompleted = true;
    }
  }

  private long mCount;
  private long mNextTicCount;
  private int  mTic;
  private long mExpectedCount;
  private boolean mCompleted;
}

