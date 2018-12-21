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

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.io.NullWritable;

import org.apache.hadoop.mapreduce.JobContext;
import org.apache.hadoop.mapreduce.OutputCommitter;
import org.apache.hadoop.mapreduce.RecordWriter;
import org.apache.hadoop.mapreduce.TaskAttemptContext;

import org.hypertable.thriftgen.*;
import org.hypertable.thrift.ThriftClient;
import org.hypertable.thrift.SerializedCellsReader;


/**
 * Write Map/Reduce output to a table in Hypertable.
 *
 * TODO: For now we assume ThriftBroker is running on localhost on default port (15867).
 * Change this to read from configs at some point.
 * Key is not used
 */
public class SerializedCellsOutputFormat
    extends org.apache.hadoop.mapreduce.OutputFormat<NullWritable, BytesWritable> {
  private static final Log log = LogFactory.getLog(OutputFormat.class);

  public static final String NAMESPACE = "hypertable.mapreduce.output.namespace";

  public static final String TABLE = "hypertable.mapreduce.output.table";

  public static final String MUTATOR_FLAGS = "hypertable.mapreduce.output.mutator-flags";

  public static final String MUTATOR_FLUSH_INTERVAL = "hypertable.mapreduce.output.mutator-flush-interval";
  public static final String THRIFT_FRAMESIZE = "hypertable.mapreduce.thriftclient.framesize";
  public static final String THRIFT_FRAMESIZE2 = "hypertable.mapreduce.thriftbroker.framesize";

  /**
   * Write reducer output to HT via Thrift interface
   *
   */
  protected class RecordWriter
  extends org.apache.hadoop.mapreduce.RecordWriter<NullWritable, BytesWritable > {
    private ThriftClient mClient;
    private long mMutator;
    private long mNamespace;
    private String namespace;
    private String table;

    /**
     * Opens a client and mutator to specified table
     *
     * @param namespace Namespace which contains the HT table
     * @param table name of HT table
     * @param flags mutator flags
     * @param flush_interval used for periodic flush mutators
     * @param framesize maximum thrift framesize
     */
    public RecordWriter(String namespace, String table, int flags,
            int flush_interval, int framesize)
      throws IOException {
      try {
        //TODO: read this from HT configs
        this.namespace = namespace;
        this.table = table;
        if (framesize != 0)
          mClient = ThriftClient.create("localhost", 15867, 1600000,
                  true, framesize);
        else
          mClient = ThriftClient.create("localhost", 15867);
        mNamespace = mClient.open_namespace(namespace);
        mMutator = mClient.open_mutator(mNamespace, table, flags, flush_interval);
      }
      catch (Exception e) {
        log.error(e);
        throw new IOException("Unable to open thrift mutator - " + e.toString());
      }
    }

    /**
     * Ctor with default flags=NO_LOG_SYNC and flush interval set to 0
     */
    public RecordWriter(String namespace, String table) throws IOException {
      this(namespace, table, MutatorFlag.NO_LOG_SYNC.getValue(), 0, 0);
    }

    /**
     * Ctor with default flush interval set to 0
     */
    public RecordWriter(String namespace, String table, int flags)
        throws IOException {
      this(namespace, table, flags, 0, 0);
    }

    /**
     * Close mutator and client
     * @param ctx
     */
    @Override
    public void close(TaskAttemptContext ctx) throws IOException {
      try {
        mClient.close_mutator(mMutator);
        mClient.close_namespace(mNamespace);
      }
      catch (Exception e) {
        log.error(e);
        throw new IOException("Unable to close thrift mutator & namespace- "
                + e.toString());
      }
    }

    /**
     * Write data to HT
     */
    @Override
    public void write(NullWritable key, BytesWritable value) throws IOException {
      try {
        mClient.mutator_set_cells_serialized(mMutator, ByteBuffer.wrap(value.getBytes()), false);
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
  public org.apache.hadoop.mapreduce.RecordWriter<NullWritable, BytesWritable>
      getRecordWriter(TaskAttemptContext ctx)
    throws IOException {
    String namespace = ctx.getConfiguration().get(OutputFormat.NAMESPACE);
    String table = ctx.getConfiguration().get(OutputFormat.TABLE);
    int flags = ctx.getConfiguration().getInt(OutputFormat.MUTATOR_FLAGS, 0);
    int flush_interval = ctx.getConfiguration().getInt(OutputFormat.MUTATOR_FLUSH_INTERVAL, 0);
    int framesize = ctx.getConfiguration().getInt(OutputFormat.THRIFT_FRAMESIZE, 0);
    if (framesize == 0)
      framesize = ctx.getConfiguration().getInt(OutputFormat.THRIFT_FRAMESIZE2, 0);

    try {
      return new RecordWriter(namespace, table, flags, flush_interval, framesize);
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
      //mClient.get_table_id();
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

