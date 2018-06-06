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
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.lang.System;
import java.nio.DoubleBuffer;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;

public class DiscreteRandomGeneratorZipf extends DiscreteRandomGenerator {

  public DiscreteRandomGeneratorZipf(int min, int max, int seed, double s) {
    super(min, max, seed);
    mS = s;
  }

  public DiscreteRandomGeneratorZipf(String cmfFile) throws FileNotFoundException, IOException {
    super(cmfFile);
  }

  protected double pmf(long val) {
    if (!mInitialized) {
      mNorm = (1.0-mS)/(Math.pow((double)(mValueCount+1), 1.0-mS));
      mInitialized = true;
    }
    assert val>=0 && val <= mValueCount+1;
    val++;
    double prob = mNorm/Math.pow((double)val, mS);
    return prob;
  }

  static String usage[] = {
    "",
    "usage: DiscreteRandomGeneratorZipf [OPTIONS] <minimum> <maximum>",
    "",
    "OPTIONS:",
    "  --cmf-file <fname>            File containing CMF array",
    "  --generate-cmf-file <fname>   Write CMF array to <fname>",
    "  --s <svalue>                  Specifies the s value for the zipf curve (default = 0.8)",
    "",
    "This program can be used to display samples from, or generate a file",
    "containing the cumulative mass function data for a Zipfian distributation.",
    null
  };

  public static void main(String args[]) {
    String cmfFile = null;
    int seed = 1;
    double svalue = 0.8;
    int minimum = -1;
    int maximum = -1;
    boolean generate = false;
    DiscreteRandomGeneratorZipf zipf;

    try {

      for (int i=0; i<args.length; i++) {
        if (args[i].equals("--generate-cmf-file")) {
          cmfFile = args[i+1];
          i++;
          generate = true;
        }
        else if (args[i].equals("--cmf-file")) {
          cmfFile = args[i+1];
          i++;
        }
        else if (args[i].equals("--s")) {
          svalue = Double.valueOf(args[i+1]);
        }
        else if (minimum == -1) {
          minimum = Integer.valueOf(args[i]);
        }
        else if (maximum == -1) {
          maximum = Integer.valueOf(args[i]);
        }
        else
          Usage.DumpAndExit(usage);
      }

      if (maximum == -1)
        Usage.DumpAndExit(usage);

      if (generate) {
        zipf = new DiscreteRandomGeneratorZipf(minimum, maximum, seed, svalue);
        zipf.setSeed(seed);
        zipf.writeCmfFile(cmfFile);
      }
      else {
        if (cmfFile == null)
          zipf = new DiscreteRandomGeneratorZipf(minimum, maximum, seed, svalue);
        else {
          System.out.println("Loading file " + cmfFile);
          zipf = new DiscreteRandomGeneratorZipf(cmfFile);
        }
        for (int i=0; i<1000000; i++) 
          System.out.println(zipf.getSample());
      }
    }
    catch (Exception e) {
      e.printStackTrace();
    }

  }

  private boolean mInitialized = false;
  double mS;
  double mNorm;
}