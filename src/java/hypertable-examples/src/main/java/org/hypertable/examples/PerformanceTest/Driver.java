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

package org.hypertable.examples.PerformanceTest;

import java.io.IOException;
import java.lang.Math;
import java.util.logging.Logger;

import org.hypertable.Common.DiscreteRandomGeneratorZipf;

public abstract class Driver {

  static final Logger log = Logger.getLogger("org.hypertable");

  public void setup(Setup setup) {
    mSetup = setup;
    mResult = new Result();

    try {

      if (mSetup.distribution == Setup.Distribution.ZIPFIAN) {
        if (mSetup.cmfFile != null || mSetup.cmfFile.equals("")) {
          System.out.printf("Loading Zipfian CMF data from file " + mSetup.cmfFile);
          mZipf = new DiscreteRandomGeneratorZipf(mSetup.cmfFile);
        }
        else {
          if (mSetup.distributionRange == 0) {
            System.out.println("Distribution range must be specified for Zipfian random distribution");
            System.exit(-1);
          }
          mZipf = new DiscreteRandomGeneratorZipf(0, (int)mSetup.distributionRange, 1, 0.8);
        }
        mZipfianMultiplier = mSetup.keyMax / mZipf.distributionRange();
        mZipf.setSeed( System.nanoTime() );
      }
    }
    catch (Exception e) {
      e.printStackTrace();
      System.exit(-1);
    }
  }

  protected long getRandomLong() {
    return Math.abs(mCommon.random.nextLong());
  }

  public abstract void teardown();

  public abstract void runTask(Task task) throws IOException;

  public Result getResult() { return mResult; }

  protected Result mResult;
  protected DriverCommon mCommon = new DriverCommon();
  protected Setup mSetup;
  protected DiscreteRandomGeneratorZipf mZipf;
  protected long mZipfianMultiplier = 0;
}
