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
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;
import java.util.Random;

import org.apache.thrift.transport.TTransportException;
import org.apache.thrift.TException;

import org.hypertable.Common.DiscreteRandomGeneratorZipf;
import org.hypertable.Common.Checksum;
import org.hypertable.thriftgen.*;
import org.hypertable.thrift.SerializedCellsFlag;
import org.hypertable.thrift.SerializedCellsReader;
import org.hypertable.thrift.SerializedCellsWriter;
import org.hypertable.thrift.ThriftClient;

public class DriverThreadHypertable extends Thread {

  static final Logger log = Logger.getLogger("org.hypertable");

  public static final int CLIENT_BUFFER_SIZE = 1024*1024*12;

  public DriverThreadHypertable(String namespace, String table, DriverThreadState state) {
    mTableName = table;
    mNamespace = namespace;
    mState = state;
  }

  public void run() {

    try {

      long wait_millis = (long)mState.common.random.nextInt();
      if (wait_millis < 0)
        wait_millis = (wait_millis*-1) % 2000;
      else
        wait_millis %= 2000;
      synchronized (mState) {
        mState.wait(wait_millis);
      }
      mClient = ThriftClient.create("localhost", 15867);
      mNamespaceId = mClient.open_namespace(mNamespace);
      mMutator = mClient.open_mutator(mNamespaceId, mTableName, 0, 0);

      synchronized (mState) {
        while (!mState.finished) {
          mState.wait();
          while (!mState.updates.isEmpty()) {
            try {
              SerializedCellsWriter writer = mState.updates.remove();
              mClient.mutator_set_cells_serialized(mMutator, writer.buffer(), true);
            }
            catch (Exception e) {
              e.printStackTrace();
            }
          }
          mState.notifyAll();
        }
      }
    }
    catch (Exception e) {
      e.printStackTrace();
      System.exit(-1);
    }

  }

  ThriftClient mClient;
  String mNamespace;
  String mTableName;
  long mMutator = 0;
  long mNamespaceId = 0;
  DriverThreadState mState;
}
