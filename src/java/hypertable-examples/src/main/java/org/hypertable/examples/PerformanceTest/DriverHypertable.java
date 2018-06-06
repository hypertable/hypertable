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
import java.util.Vector;

import org.apache.thrift.transport.TTransportException;
import org.apache.thrift.TException;

import org.hypertable.Common.Checksum;
import org.hypertable.thriftgen.*;
import org.hypertable.thrift.SerializedCellsFlag;
import org.hypertable.thrift.SerializedCellsReader;
import org.hypertable.thrift.SerializedCellsWriter;
import org.hypertable.thrift.ThriftClient;

public class DriverHypertable extends Driver {

  static final Logger log = Logger.getLogger("org.hypertable");

  public static final int CLIENT_BUFFER_SIZE = 1024*1024*12;

  public static final int DEFAULT_THRIFTBROKER_PORT = 15867;

  public DriverHypertable() throws TTransportException, TException {
  }

  public DriverHypertable(int broker_port) {
      mThriftBrokerPort = broker_port;
  }

  protected void finalize() {
    if (mSetup.parallelism == 0) {
      try {
        if (mNamespaceId != 0)
          mClient.namespace_close(mNamespaceId);
      }
      catch (Exception e) {
        System.out.println("Unable to close namespace - " + mNamespace +
                           e.getMessage());
        System.exit(-1);
      }
    }
  }

  public void setup(Setup setup) {
    super.setup(setup);

    if (mSetup.parallelism == 0) {
      mCellsWriter = new SerializedCellsWriter(CLIENT_BUFFER_SIZE);
      try {
        mClient = ThriftClient.create("localhost", mThriftBrokerPort);
        mNamespace = "/";
        mNamespaceId = mClient.namespace_open(mNamespace);
      }
      catch (Exception e) {
        System.out.println("Unable to establish connection to ThriftBroker at localhost:15867 " +
                           "and open namespace '/'- " +
                           e.getMessage());
        System.exit(-1);
      }
    }

    try {

      mCommon.initializeValueData();

      if (mSetup.parallelism > 0) {
        mThreadStates = new DriverThreadState[mSetup.parallelism];
        for (int i=0; i<mSetup.parallelism; i++) {
          mThreadStates[i] = new DriverThreadState();
          mThreadStates[i].common = mCommon;
          mThreadStates[i].thread = new DriverThreadHypertable("/", mSetup.tableName, mThreadStates[i]);
          mThreadStates[i].thread.start();
        }
      }
      else {
        mCellsWriter.clear();
        if (mSetup.type == Setup.Type.WRITE || mSetup.type == Setup.Type.INCR)
          mMutator = mClient.mutator_open(mNamespaceId, mSetup.tableName, 0, 0);
      }
    }
    catch (ClientException e) {
      e.printStackTrace();
      System.exit(-1);
    }
    catch (IOException e) {
      e.printStackTrace();
      System.exit(-1);
    }
    catch (TException e) {
      e.printStackTrace();
      System.exit(-1);
    }
  }

  public void teardown() {
  }

  public void runTask(Task task) throws IOException {
    long randi;
    ByteBuffer keyByteBuf = ByteBuffer.allocate(8);
    byte [] keyBuf = keyByteBuf.array();

    long taskStartTime = System.currentTimeMillis();

    if (mSetup.type == Setup.Type.WRITE) {
      byte [] row = new byte [ mSetup.keySize ];
      byte [] family = mCommon.COLUMN_FAMILY_BYTES;
      byte [] qualifier = mCommon.COLUMN_QUALIFIER_BYTES;
      byte [] value = new byte [ mSetup.valueSize ];

      if (mSetup.parallelism == 0)
        mCellsWriter.clear();

      try {
        for (long i=task.start; i<task.end; i++) {
          if (mSetup.order == Setup.Order.RANDOM) {
            randi = getRandomLong();
            if (mSetup.keyMax != -1)
              randi %= mSetup.keyMax;
            mCommon.formatRowKey(randi, mSetup.keySize, row);
          }
          else
            mCommon.formatRowKey(i, mSetup.keySize, row);
          mCommon.fillValueBuffer(value);
          if (mSetup.parallelism > 0) {
            mCellsWriter = new SerializedCellsWriter(mSetup.keySize + family.length + qualifier.length + value.length + 32);
            if (!mCellsWriter.add(row, 0, mSetup.keySize, family, 0, family.length, qualifier, 0, qualifier.length,
                                  SerializedCellsFlag.AUTO_ASSIGN, value, 0, value.length, SerializedCellsFlag.FLAG_INSERT)) {
              System.out.println("Failed to write to SerializedCellsWriter");
              System.exit(-1);
            }
            synchronized (mThreadStates[mThreadNext]) {
              mThreadStates[mThreadNext].updates.add(mCellsWriter);
              mThreadStates[mThreadNext].notifyAll();
            }
            mThreadNext = (mThreadNext + 1) % mSetup.parallelism;
            mCellsWriter = null;
          }
          else {
            while (!mCellsWriter.add(row, 0, mSetup.keySize,
                                     family, 0, family.length,
                                     qualifier, 0, qualifier.length,
                                     SerializedCellsFlag.AUTO_ASSIGN,
                                     value, 0, value.length, SerializedCellsFlag.FLAG_INSERT)) {
              mClient.mutator_set_cells_serialized(mMutator, mCellsWriter.buffer(), false);
              mCellsWriter.clear();
            }
          }
        }
        if (mSetup.parallelism == 0) {
          if (!mCellsWriter.isEmpty())
            mClient.mutator_set_cells_serialized(mMutator, mCellsWriter.buffer(), true);
          else
            mClient.mutator_flush(mMutator);
        }
      }
      catch (Exception e) {
        e.printStackTrace();
        log.severe(e.toString());
        throw new IOException("Unable to set cell via thrift - " + e.toString());
      }
    }
    else if (mSetup.type == Setup.Type.INCR) {
      byte [] row = new byte [ mSetup.keySize ];
      byte [] family = mCommon.COLUMN_FAMILY_BYTES;
      byte [] qualifier = mCommon.COLUMN_QUALIFIER_BYTES;
      byte [] value = mCommon.INCREMENT_VALUE_BYTES;

      if (mSetup.parallelism == 0)
        mCellsWriter.clear();

      try {
        for (long i=task.start; i<task.end; i++) {
          if (mSetup.order == Setup.Order.RANDOM) {
            randi = getRandomLong();
            if (mSetup.keyMax != -1)
              randi %= mSetup.keyMax;
            mCommon.formatRowKey(randi, mSetup.keySize, row);
          }
          else
            mCommon.formatRowKey(i, mSetup.keySize, row);
          if (mSetup.parallelism > 0) {
            mCellsWriter = new SerializedCellsWriter(mSetup.keySize + family.length + qualifier.length + value.length + 32);
            if (!mCellsWriter.add(row, 0, mSetup.keySize, family, 0, family.length, qualifier, 0, qualifier.length,
                                  SerializedCellsFlag.AUTO_ASSIGN, value, 0, value.length, SerializedCellsFlag.FLAG_INSERT)) {
              System.out.println("Failed to write to SerializedCellsWriter");
              System.exit(-1);
            }
            synchronized (mThreadStates[mThreadNext]) {
              mThreadStates[mThreadNext].updates.add(mCellsWriter);
              mThreadStates[mThreadNext].notifyAll();
            }
            mThreadNext = (mThreadNext + 1) % mSetup.parallelism;
            mCellsWriter = null;
          }
          else {
            while (!mCellsWriter.add(row, 0, mSetup.keySize,
                                     family, 0, family.length,
                                     qualifier, 0, qualifier.length,
                                     SerializedCellsFlag.AUTO_ASSIGN,
                                     value, 0, value.length, SerializedCellsFlag.FLAG_INSERT)) {
              mClient.mutator_set_cells_serialized(mMutator, mCellsWriter.buffer(), false);
              mCellsWriter.clear();
            }
          }
        }
        if (mSetup.parallelism == 0) {
          if (!mCellsWriter.isEmpty())
            mClient.mutator_set_cells_serialized(mMutator, mCellsWriter.buffer(), true);
          else
            mClient.mutator_flush(mMutator);
        }
      }
      catch (Exception e) {
        e.printStackTrace();
        log.severe(e.toString());
        throw new IOException("Unable to set cell via thrift - " + e.toString());
      }
    }
    else if (mSetup.type == Setup.Type.READ) {
      String row;
      SerializedCellsReader reader = new SerializedCellsReader();

      if (mSetup.parallelism != 0) {
        System.out.println("Parallel reads not implemented");
        System.exit(1);
      }

      try {
        for (long i=task.start; i<task.end; i++) {
          if (mSetup.order == Setup.Order.RANDOM) {
            keyByteBuf.clear();
            if (mSetup.distribution == Setup.Distribution.ZIPFIAN) {
              randi = mZipf.getSample();
	      randi *= mZipfianMultiplier;
            }
            else {
              randi = getRandomLong() % mSetup.keyMax;
            }
            row = mCommon.formatRowKey(randi, mSetup.keySize);
          }
          else
            row = mCommon.formatRowKey(i, mSetup.keySize);

          long startTime = System.currentTimeMillis();
          reader.reset( mClient.get_row_serialized(mNamespaceId, mSetup.tableName, row) );
          mResult.cumulativeLatency += System.currentTimeMillis() - startTime;
          mResult.requestCount++;
          while (reader.next()) {
            mResult.itemsReturned++;
            mResult.valueBytesReturned += reader.get_value_length();
          }
        }
      }
      catch (Exception e) {
        log.severe(e.toString());
        throw new IOException("Unable to set cell via thrift - " + e.toString());
      }
    }
    else if (mSetup.type == Setup.Type.SCAN) {
      boolean eos = false;
      String start_row = mCommon.formatRowKey(task.start, mSetup.keySize);
      String end_row = mCommon.formatRowKey(task.end, mSetup.keySize);
      SerializedCellsReader reader = new SerializedCellsReader();

      if (mSetup.parallelism != 0) {
        System.out.println("Parallel scans not implemented");
        System.exit(1);
      }

      ScanSpec scan_spec = new ScanSpec();
      RowInterval ri = new RowInterval();

      ri.setStart_row(start_row);
      ri.setStart_inclusive(true);
      ri.setEnd_row(end_row);
      ri.setEnd_inclusive(false);
      scan_spec.addToRow_intervals(ri);

      try {
        long scanner = mClient.scanner_open(mNamespaceId, mSetup.tableName, scan_spec);
        while (!eos) {
          reader.reset( mClient.scanner_get_cells_serialized(scanner) );
          while (reader.next()) {
            mResult.itemsReturned++;
            mResult.valueBytesReturned += reader.get_value_length();
          }
          eos = reader.eos();
        }
        mClient.scanner_close(scanner);
      }
      catch (Exception e) {
        e.printStackTrace();
        throw new IOException("Problem fetching scan results via thrift - " + e.toString());
      }
    }

    try {
      for (int i=0; i<mSetup.parallelism; i++) {
        synchronized (mThreadStates[mThreadNext]) {
          if (!mThreadStates[mThreadNext].updates.isEmpty()) {
            mThreadStates[mThreadNext].wait();
          }
          // wait for read requests here
        }
      }
    }
    catch (InterruptedException e) {
      e.printStackTrace();
      System.exit(1);
    }

    mResult.itemsSubmitted += (task.end-task.start);
    mResult.elapsedMillis += System.currentTimeMillis() - taskStartTime;
  }

  ThriftClient mClient;
  String mNamespace;
  long mMutator;
  long mNamespaceId=0;
  SerializedCellsWriter mCellsWriter;
  DriverThreadState [] mThreadStates;
  int mThreadNext = 0;
  int mThriftBrokerPort = DEFAULT_THRIFTBROKER_PORT;
}
