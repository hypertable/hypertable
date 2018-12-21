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

import org.apache.hadoop.conf.Configuration;

import org.apache.hadoop.hbase.HBaseConfiguration;
import org.apache.hadoop.hbase.client.Get;
import org.apache.hadoop.hbase.client.HBaseAdmin;
import org.apache.hadoop.hbase.client.HTable;
import org.apache.hadoop.hbase.client.Put;
import org.apache.hadoop.hbase.client.Result;
import org.apache.hadoop.hbase.client.ResultScanner;
import org.apache.hadoop.hbase.client.Scan;
import org.apache.hadoop.hbase.KeyValue;

import org.hypertable.Common.Checksum;
import org.hypertable.thriftgen.*;
import org.hypertable.thrift.ThriftClient;

public class DriverHBase extends Driver {

  static final Logger log = Logger.getLogger("org.hypertable");

  public DriverHBase() throws IOException {
      this.conf = HBaseConfiguration.create();
      this.admin = new HBaseAdmin( this.conf );
  }

  public void setup(Setup setup) {
    super.setup(setup);
    try {
      this.table = new HTable(conf, mSetup.tableName);
      this.table.setAutoFlush(false);
      this.table.setWriteBufferSize(1024*1024*12);

      if (mSetup.type == Setup.Type.WRITE) {
        mCommon.initializeValueData();
      }
    }
    catch (IOException e) {
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
    org.apache.hadoop.hbase.KeyValue [] kvs = null;

    long taskStartTime = System.currentTimeMillis();

    if (mSetup.type == Setup.Type.WRITE) {
      Put put = null;
      byte [] value = null;

      try {
        for (long i=task.start; i<task.end; i++) {
          if (mSetup.order == Setup.Order.RANDOM) {
            randi = getRandomLong();
            if (mSetup.keyMax != -1)
              randi %= mSetup.keyMax;
            put = new Put( mCommon.formatRowKey(randi, mSetup.keySize).getBytes() );
          }
          else
            put = new Put( mCommon.formatRowKey(i, mSetup.keySize).getBytes() );
          value = new byte [ mSetup.valueSize ];
          mCommon.fillValueBuffer(value);
          put.add(mCommon.COLUMN_FAMILY_BYTES, mCommon.COLUMN_QUALIFIER_BYTES, value);
          table.put(put);
        }
        this.table.flushCommits();
      }
      catch (Exception e) {
        log.severe(e.toString());
        throw new IOException("Unable to set cell via thrift - " + e.toString());
      }
    }
    else if (mSetup.type == Setup.Type.READ) {
      Get get = null;
      org.apache.hadoop.hbase.client.Result result = null;
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
            get = new Get( mCommon.formatRowKey(randi, mSetup.keySize).getBytes() );
          }
          else
            get = new Get( mCommon.formatRowKey(i, mSetup.keySize).getBytes() );
          get.addColumn(mCommon.COLUMN_FAMILY_BYTES, mCommon.COLUMN_QUALIFIER_BYTES);
          long startTime = System.currentTimeMillis();          
          result = table.get(get);
          mResult.cumulativeLatency += System.currentTimeMillis() - startTime;
          mResult.requestCount++;
          if (result != null) {
	      kvs = result.raw();
	      if (kvs != null) {
		  for (KeyValue kv : kvs) {
		      mResult.itemsReturned++;
		      mResult.valueBytesReturned += kv.getValueLength();
		  }
	      }
          }
        }
      }
      catch (Exception e) {
        e.printStackTrace();
        log.severe(e.toString());
        throw new IOException("Unable to set KeyValue via thrift - " + e.toString());
      }
    }
    else if (mSetup.type == Setup.Type.SCAN) {
      Scan scan = new Scan(mCommon.formatRowKey(task.start, mSetup.keySize).getBytes(),
                           mCommon.formatRowKey(task.end, mSetup.keySize).getBytes());
      scan.setMaxVersions();
      this.table.setScannerCaching(mSetup.scanBufferSize/(mSetup.keySize+10+mSetup.valueSize));
      ResultScanner scanner = table.getScanner(scan);
      org.apache.hadoop.hbase.client.Result result = null;

      result = scanner.next();
      while (result != null) {
	  kvs = result.raw();
	  if (kvs != null) {
	      for (KeyValue kv : kvs) {
		  mResult.itemsReturned++;
		  mResult.valueBytesReturned += kv.getValueLength();
	      }
	  }
	  result = scanner.next();
      }
      scanner.close();
    }
    
    mResult.itemsSubmitted += (task.end-task.start);
    mResult.elapsedMillis += System.currentTimeMillis() - taskStartTime;
  }

  protected volatile Configuration conf;
  protected HBaseAdmin admin;
  protected HTable table;
}
