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

import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.System;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.util.Random;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.MappedByteBuffer;
import java.io.FileNotFoundException;
import java.io.IOException;

public abstract class DiscreteRandomGenerator {

  public DiscreteRandomGenerator(int min, int max, int seed) {
    mMinValue = min;
    mMaxValue = max;
    mOrigSeed = seed;
    mRandom = new Random(mOrigSeed);
  }

  public DiscreteRandomGenerator(String cmfFile) throws FileNotFoundException, IOException {
    File file = new File(cmfFile);
    FileChannel roChannel = new RandomAccessFile(file, "r").getChannel();
    MappedByteBuffer buf = roChannel.map(FileChannel.MapMode.READ_ONLY, 0, 12);
    mMinValue = buf.getInt();
    mMaxValue = buf.getInt();
    mValueCount = mMaxValue-mMinValue;
    mRandom = new Random(1);
    mNumberBuffer = roChannel.map(FileChannel.MapMode.READ_ONLY, 8, (int)(mValueCount*8));
    long cmfOffset = 8+(mValueCount*8);
    mCmfBuffer = roChannel.map(FileChannel.MapMode.READ_ONLY, cmfOffset, roChannel.size()-cmfOffset);
  }

  void writeCmfFile(String cmfFile) {
    if (mCmfBuffer == null)
      generateCmf();

    try {
      FileChannel wChannel = new FileOutputStream(cmfFile, false).getChannel();
      ByteBuffer buf = ByteBuffer.allocate(8);
      buf.putInt(mMinValue);
      buf.putInt(mMaxValue);
      buf.flip();
      wChannel.write(buf);
      // Write Number vector
      mNumberBuffer.limit( mNumberBuffer.capacity() );
      mNumberBuffer.position(0);
      wChannel.write(mNumberBuffer);
      // Write CMF data
      mCmfBuffer.limit( mCmfBuffer.capacity() );
      mCmfBuffer.position(0);
      wChannel.write(mCmfBuffer);
      wChannel.close();
    }
    catch (Exception e) {
      e.printStackTrace();
    }
  }

  public void setSeed(long seed) {
    if (mCmfBuffer == null)
      generateCmf();
    mRandom.setSeed(seed);
  }

  public int distributionRange() {
    return mMaxValue-mMinValue;
  }

  public long getSample() {

    if (mCmfBuffer == null)
      generateCmf();

    int upper = mValueCount;
    int lower = 0;
    int ii = 0;

    double rand = mRandom.nextDouble();

    // do a binary search through cmf to figure out which index in cmf
    // rand lies in. this will transform the uniform[0,1] distribution into
    // the distribution specified in mCmf
    try {
      while (ii < (mValueCount-1)) {

        ii = (upper + lower)/2;
        if (mCmfBuffer.getDouble(ii*8) >= rand) {
          if (ii == 0 || mCmfBuffer.getDouble((ii-1)*8) <= rand)
            break;
          else {
            upper = ii - 1;
            continue;
          }
        }
        else {
          lower = ii + 1;
          continue;
        }
      }
    }
    catch (IndexOutOfBoundsException e) {
      System.err.println("index = " + ii + " length = " + mValueCount + " cmfCapacity=" + mCmfBuffer.capacity());
      e.printStackTrace();
    }

    return mNumberBuffer.getLong(ii*8);
  }

  protected void generateCmf() {
    int ii;
    double norm_const;
    
    mValueCount = mMaxValue-mMinValue;

    mCmfBuffer = ByteBuffer.allocate((mValueCount+1)*8);

    loadNumberBuffer();

    double prev = pmf(0);
    mCmfBuffer.putDouble(prev);
    for (ii=1; ii < mValueCount+1 ;++ii) {
      prev = prev + pmf(ii);
      mCmfBuffer.putDouble(prev);
    }

    norm_const = prev;
    // renormalize cmf
    for (ii=0; ii < mValueCount+1 ;++ii) {
      mCmfBuffer.putDouble(ii*8, mCmfBuffer.getDouble(ii*8) / norm_const);
    }
  }

  private void loadNumberBuffer() {
    int ii;
    mNumberBuffer = ByteBuffer.allocate(mValueCount*8);
    for (int i=0; i<mValueCount; i++)
      mNumberBuffer.putLong(i*8, i);
    long tmpval;
    for (int i=0; i<mValueCount; i++) {
      tmpval = mNumberBuffer.getLong(i*8);
      ii = Math.abs(mRandom.nextInt()) % mValueCount;
      mNumberBuffer.putLong(i*8, mNumberBuffer.getLong(ii*8));
      mNumberBuffer.putLong(ii*8, tmpval);
    }
  }

  abstract protected double pmf(long val);

  protected int mMinValue;
  protected int mMaxValue;
  protected int mOrigSeed;
  protected int mValueCount;
  private Random mRandom;
  protected ByteBuffer mNumberBuffer;
  protected ByteBuffer mCmfBuffer;
}
