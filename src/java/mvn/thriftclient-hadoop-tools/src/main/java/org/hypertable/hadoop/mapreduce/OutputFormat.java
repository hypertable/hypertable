/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

package org.hypertable.hadoop.mapreduce;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.text.ParseException;
import java.util.Random;
import java.util.Vector;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.mapreduce.JobContext;
import org.apache.hadoop.mapreduce.OutputCommitter;
import org.apache.hadoop.mapreduce.RecordWriter;
import org.apache.hadoop.mapreduce.TaskAttemptContext;

import org.hypertable.Common.HostSpecification;
import org.hypertable.thrift.SerializedCellsFlag;
import org.hypertable.thrift.SerializedCellsWriter;
import org.hypertable.thrift.ThriftClient;
import org.hypertable.thriftgen.*;


/**
 * Write Map/Reduce output to a table in Hypertable.
 *
 * Change this to read from configs at some point.
 * Key is not used but output value must be a KeyWritable
 */
public class OutputFormat extends org.apache.hadoop.mapreduce.OutputFormat<KeyWritable, BytesWritable> {
  private static final Log log = LogFactory.getLog(OutputFormat.class);

  public static final String NAMESPACE = "hypertable.mapreduce.output.namespace";
  public static final String TABLE = "hypertable.mapreduce.output.table";
  public static final String MUTATOR_FLAGS = "hypertable.mapreduce.output.mutator-flags";
  public static final String MUTATOR_FLUSH_INTERVAL = "hypertable.mapreduce.output.mutator-flush-interval";
  public static final String THRIFT_FRAMESIZE = "hypertable.mapreduce.thriftclient.framesize";
  public static final String THRIFT_FRAMESIZE2 = "hypertable.mapreduce.thriftbroker.framesize";
  public static final String THRIFT_HOST = "hypertable.mapreduce.thriftbroker.host";
  public static final String THRIFT_PORT = "hypertable.mapreduce.thriftbroker.port";

  /**
   * Gets the ThriftBroker host name.
   * Obtains the ThriftBroker host name from the
   * "hypertable.mapreduce.thriftbroker.host" property.  The value of this
   * property can either be a host name or IP address, or it can be a host
   * specification.  If it is a host specification, it is expanded into a vector
   * of host names, one of which is chosen at random.  The default value of
   * "localhost" is used if the host property was not supplied.
   * @param conf Job configuration
   * @return Host name of ThriftBroker
   */
  private static String getThriftHost(Configuration conf) throws ParseException {
    String host = "localhost";
    String spec = conf.get(THRIFT_HOST);
    if (spec != null) {
      HostSpecification hs = new HostSpecification(spec);
      Vector<String> expanded = hs.expand();
      if (expanded.size() == 1)
        host = expanded.elementAt(0);
      else
        host = expanded.elementAt((new Random()).nextInt(expanded.size()));
    }
    return host;
  }

  /**
   * Write reducer output to HT via Thrift interface
   *
   */
  protected static class HypertableRecordWriter extends RecordWriter<KeyWritable, BytesWritable > {
    private ThriftClient mClient;
    private long mNamespace;
    private long mMutator;
    private SerializedCellsWriter mCellsWriter;
    private String table;
    private String namespace;

    /**
     * Opens a client and a mutator to specified table
     *
     * @param client Thrift client
     * @param namespace HT namespace which contains the table
     * @param table name of HT table
     * @param flags mutator flags
     * @param flush_interval used for periodic flush mutators
     */
    public HypertableRecordWriter(ThriftClient client, String namespace,
                                  String table, int flags,
                                  int flush_interval) throws IOException {
      try {
        this.namespace = namespace;
        this.table = table;
        this.mClient = client;
        mNamespace = mClient.namespace_open(namespace);
        mMutator = mClient.mutator_open(mNamespace, table, flags, flush_interval);
        mCellsWriter = new SerializedCellsWriter(6*1024*1024);
      }
      catch (Exception e) {
        log.error(e);
        throw new IOException("Unable to open thrift mutator - " + e.toString());
      }
    }

    /**
     * Close mutator and client
     * @param ctx
     */
    @Override
      public void close(TaskAttemptContext ctx) throws IOException {
      try {
        if (!mCellsWriter.isEmpty()) {
          mClient.mutator_set_cells_serialized(mMutator, mCellsWriter.buffer(), true);
          mCellsWriter.clear();
        }
        mClient.mutator_close(mMutator);
        mClient.namespace_close(mNamespace);
        mClient.close();
      }
      catch (Exception e) {
        log.error(e);
        throw new IOException("Unable to close thrift mutator & namespace- " + e.toString());
      }
    }

    /**
     * Write data to HT
     */
    @Override
      public void write(KeyWritable key, BytesWritable value) throws IOException {
      try {
        key.convert_strings_to_buffers();
        while (!mCellsWriter.add(key.getRowBytes(), key.getRowOffset(), key.getRowLength(),
                                 key.getColumnFamilyBytes(), key.getColumnFamilyOffset(), key.getColumnFamilyLength(),
                                 key.getColumnQualifierBytes(), key.getColumnQualifierOffset(), key.getColumnQualifierLength(),
                                 key.getTimestamp(), value.getBytes(), 0, value.getLength(),
                                 SerializedCellsFlag.FLAG_INSERT)) {
          mClient.mutator_set_cells_serialized(mMutator, mCellsWriter.buffer(), false);
          mCellsWriter.clear();
        }
      }
      catch (Exception e) {
        log.error(e);
        throw new IOException("Unable to write cell - " + e.toString());
      }
    }
  }

  /**
   * Create a record writer
   */
  public RecordWriter<KeyWritable, BytesWritable> getRecordWriter(TaskAttemptContext ctx)
    throws IOException {
    String namespace = ctx.getConfiguration().get(OutputFormat.NAMESPACE);
    String table = ctx.getConfiguration().get(OutputFormat.TABLE);
    int flags = ctx.getConfiguration().getInt(OutputFormat.MUTATOR_FLAGS, 0);
    int flush_interval = ctx.getConfiguration().getInt(OutputFormat.MUTATOR_FLUSH_INTERVAL, 0);
    int framesize = ctx.getConfiguration().getInt(OutputFormat.THRIFT_FRAMESIZE, 0);
    if (framesize == 0)
      framesize = ctx.getConfiguration().getInt(OutputFormat.THRIFT_FRAMESIZE2, 0);

    try {
      String host = getThriftHost(ctx.getConfiguration());
      int port = ctx.getConfiguration().getInt(THRIFT_PORT, 15867);
      ThriftClient client;
      if (framesize != 0)
        client = ThriftClient.create(host, port, 1600000, true, framesize);
      else
        client = ThriftClient.create(host, port);
      return new HypertableRecordWriter(client, namespace, table, flags, flush_interval);
    }
    catch (Exception e) {
      log.error(e);
      throw new IOException("Unable to access RecordWriter - " + e.toString());
    }
  }

  /**
   * TODO: Do something meaningful here
   * Make sure the table exists
   *
   */
  @Override
    public void checkOutputSpecs(JobContext ctx) throws IOException {
    try {
      //mClient.table_get_id();
    }
    catch (Exception e) {
      log.error(e);
      throw new IOException("Unable to get table id - " + e.toString());
    }
  }

  /**
   *
   *
   */
  @Override
    public OutputCommitter getOutputCommitter(TaskAttemptContext ctx) {
    return new org.hypertable.hadoop.mapreduce.OutputCommitter();
  }
}

