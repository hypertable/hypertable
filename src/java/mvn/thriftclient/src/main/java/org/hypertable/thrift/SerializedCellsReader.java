/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is distributed under the Apache Software License
 * (http://www.apache.org/licenses/)
 */


package org.hypertable.thrift;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.InvalidMarkException;

public class SerializedCellsReader {

  public SerializedCellsReader() {
    mBase = null;
    mBaseOffset = 0;
  }

  public SerializedCellsReader(byte [] buf) {
    mBase = buf;
    mBaseOffset = 0;
    if (buf != null) {
      mBuf = ByteBuffer.wrap(buf);
      mBuf.order(ByteOrder.LITTLE_ENDIAN);
      int version = mBuf.getInt();
      if (version != SerializedCellsFlag.VERSION)
        throw new AssertionError("SerializedCells version mismatch, expected "
                + SerializedCellsFlag.VERSION + ", got " + version);
    }
  }

  public SerializedCellsReader(ByteBuffer buf) {
    if (buf != null)
      reset(buf);
    else {
      mBase = null;
      mBaseOffset = 0;
    }
  }

  public void reset(byte [] buf) {
    mBase = buf;
    mBaseOffset = 0;
    mBuf = ByteBuffer.wrap(buf);
    mBuf.order(ByteOrder.LITTLE_ENDIAN);
    mEob = false;
    mRow = null;
    mColumnFamily = null;
    mColumnQualifier = null;
    mValue = null;
    mCellFlag = 0;
    int version = mBuf.getInt();
    if (version != SerializedCellsFlag.VERSION)
      throw new AssertionError("SerializedCells version mismatch, expected "
              + SerializedCellsFlag.VERSION + ", got " + version);
  }

  public void reset(ByteBuffer buf) {
    try {
      buf.reset();
    }
    catch (InvalidMarkException e) {
      buf.mark();
    }
    mBase = buf.array();
    mBaseOffset = buf.arrayOffset();
    mBuf = buf;
    mBuf.order(ByteOrder.LITTLE_ENDIAN);
    mEob = false;
    mRow = null;
    mColumnFamily = null;
    mColumnQualifier = null;
    mValue = null;
    mCellFlag = 0;
    int version = mBuf.getInt();
    if (version != SerializedCellsFlag.VERSION)
      throw new AssertionError("SerializedCells version mismatch, expected "
              + SerializedCellsFlag.VERSION + ", got " + version);
  }

  public boolean next() {
    int offset;
    long revision;

    if (mEob)
      return false;

    mFlag = mBuf.get();

    if ((mFlag & SerializedCellsFlag.EOB) != 0) {
      mEob = true;
      return false;
    }

    if ((mFlag & SerializedCellsFlag.HAVE_TIMESTAMP) != 0)
      mTimestamp = mBuf.getLong();

    if ((mFlag & SerializedCellsFlag.HAVE_REVISION) != 0 &&
        (mFlag & SerializedCellsFlag.REV_IS_TS) == 0)
      revision = mBuf.getLong();

    // row
    int row_offset = mBuf.position();
    for (offset=mBuf.position(); mBuf.get(offset)!=0; offset++)
      ;
    int length = offset - row_offset;
    mBuf.position(offset+1); // skip \0
    if (length > 0) {
      mRowOffset = row_offset;
      mRowLength = length;
      mRow = null;
    }

    // column_family
    mColumnFamily = null;
    mColumnFamilyOffset = mBuf.position();
    for (offset=mBuf.position(); mBuf.get(offset)!=0; offset++)
      ;
    mColumnFamilyLength = offset - mColumnFamilyOffset;
    mBuf.position(offset+1); // skip \0

    // column_qualifier
    mColumnQualifier = null;
    mColumnQualifierOffset = mBuf.position();
    for (offset=mBuf.position(); mBuf.get(offset)!=0; offset++)
      ;
    mColumnQualifierLength = offset - mColumnQualifierOffset;
    mBuf.position(offset+1); // skip \0

    mValue = null;
    mValueLength = mBuf.getInt();
    mValueOffset = mBuf.position();
    if (mValueLength < 0) {
      System.out.println("vo=" + mValueOffset + ", vl=" + mValueLength + ", bl=" + mBuf.limit());
      System.out.println("ro=" + mRowOffset + ", rl=" + mRowLength);
      System.out.println("cfo=" + mColumnFamilyOffset + ", cfl=" + mColumnFamilyLength);
      System.out.println("cqo=" + mColumnQualifierOffset + ", cql=" + mColumnQualifierLength);
      System.exit(-1);
    }

    mBuf.position(mValueOffset+mValueLength);

    mCellFlag = mBuf.get();

    return true;
  }

  public byte [] get_row() {
    if (mRow == null) {
      mRow = new byte [ mRowLength ];
      System.arraycopy(mBase, mBaseOffset+mRowOffset, mRow, 0, mRowLength);
    }
    return mRow;
  }

  public int get_row_length() { return mRowLength; }

  public byte [] get_column_family() {
    if (mColumnFamily == null) {
      mColumnFamily = new byte [ mColumnFamilyLength ];
      System.arraycopy(mBase, mBaseOffset+mColumnFamilyOffset, mColumnFamily, 0, mColumnFamilyLength);
    }
    return mColumnFamily;
  }

  public int get_column_family_length() { return mColumnFamilyLength; }

  public byte [] get_column_qualifier() {
    if (mColumnQualifier == null && mColumnQualifierLength > 0) {
      mColumnQualifier = new byte [ mColumnQualifierLength ];
      System.arraycopy(mBase, mBaseOffset+mColumnQualifierOffset, mColumnQualifier, 0, mColumnQualifierLength);
    }
    return mColumnQualifier;
  }

  public int get_column_qualifier_length() { return mColumnQualifierLength; }

  public long get_timestamp() {
    return mTimestamp;
  }

  public byte [] get_value() {
    if (mValue == null && mValueLength > 0) {
      mValue = new byte [ mValueLength ];
      System.arraycopy(mBase, mBaseOffset+mValueOffset, mValue, 0, mValueLength);
    }
    return mValue;
  }

  public int get_value_length() { return mValueLength; }

  public byte get_flag() { return mCellFlag; }

  public boolean eos() {
    return (mFlag & SerializedCellsFlag.EOS) != 0;
  }

  private boolean mEob = false;
  private byte [] mBase;
  private int mBaseOffset;
  private ByteBuffer mBuf;
  private byte mFlag;
  private byte [] mRow;
  private int mRowOffset;
  private int mRowLength;
  private byte [] mColumnFamily;
  private int mColumnFamilyOffset;
  private int mColumnFamilyLength;
  private byte [] mColumnQualifier;
  private int mColumnQualifierOffset;
  private int mColumnQualifierLength;
  private byte [] mValue;
  private int mValueOffset;
  private int mValueLength;
  private long mTimestamp;
  private byte mCellFlag;
}
