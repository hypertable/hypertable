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


package org.hypertable.FsBroker.Lib;

import org.hypertable.Common.Status;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

/**
 * This class acts as a synchronized HashMap that maps open file descriptors to
 * OpenFileData objects
 */
public class StatusManager {

  /** Constructor.
   */
  public StatusManager() {
    mStatus = new Status();
    mCurrentStatus = mStatus.get();
  }

  public synchronized void setReadException(Exception e) {
    Status.Code code = Status.Code.CRITICAL;
    if (mCurrentStatus == Status.Code.OK || mCurrentStatus != Status.Code.CRITICAL) {
      mCurrentStatus = code;
      mStatus.set(code, e.toString());
    }
    mLastReadError = System.currentTimeMillis();
  }

  public synchronized void setWriteException(Exception e) {
    Status.Code code = Status.Code.CRITICAL;
    if (mCurrentStatus == Status.Code.OK || mCurrentStatus != Status.Code.CRITICAL) {
      mCurrentStatus = code;
      mStatus.set(code, e.toString());
    }
    mLastWriteError = System.currentTimeMillis();
  }

  public void setReadStatus(Status.Code code, String text) {
    if (code == Status.Code.OK) {
      clearStatus();
      return;
    }
    synchronized (this) {
      if (mCurrentStatus == Status.Code.OK ||
          (mCurrentStatus != Status.Code.CRITICAL && code == Status.Code.CRITICAL)) {
        mCurrentStatus = code;
        mStatus.set(code, text);
      }
      mLastReadError = System.currentTimeMillis();
    }
  }

  public void setWriteStatus(Status.Code code, String text) {
    if (code == Status.Code.OK) {
      clearStatus();
      return;
    }
    synchronized (this) {
      if (mCurrentStatus == Status.Code.OK ||
          (mCurrentStatus != Status.Code.CRITICAL && code == Status.Code.CRITICAL)) {
        mCurrentStatus = code;
        mStatus.set(code, text);
      }
      mLastWriteError = System.currentTimeMillis();
    }
  }

  public void clearStatusImmediately() {
    if (mCurrentStatus == Status.Code.OK)
      return;
    synchronized (this) {
      mStatus.set(Status.Code.OK, null);
      mCurrentStatus = Status.Code.OK;
    }
  }

  public void clearStatus() {
    if (mCurrentStatus == Status.Code.OK)
      return;
    long now = System.currentTimeMillis();
    synchronized (this) {
      if (now - mLastReadError > CLEAR_CHANGE_INTERVAL &&
          now - mLastWriteError > CLEAR_CHANGE_INTERVAL) {
        mStatus.set(Status.Code.OK, null);
        mCurrentStatus = Status.Code.OK;
      }
    }
  }

  public Status get() { return mStatus; }

  private final long CLEAR_CHANGE_INTERVAL = 60000;
  private Status mStatus;
  private long mLastReadError;
  private long mLastWriteError;
  private Status.Code mCurrentStatus;
}

