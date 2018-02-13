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

package org.hypertable.hadoop.mapred;

import java.io.IOException;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.util.Progressable;
import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.io.NullWritable;

import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.mapred.RecordWriter;
import org.apache.hadoop.mapred.Reporter;
import org.apache.hadoop.mapred.JobConfigurable;

import org.hypertable.thriftgen.*;
import org.hypertable.thrift.ThriftClient;
import org.hypertable.thrift.SerializedCellsWriter;
import org.hypertable.hadoop.util.Row;

/**
 * Write Map/Reduce output Row to a table in Hypertable.
 *
 * TODO: For now we assume ThriftBroker is running on localhost on default port (15867).
 * Change this to read from configs at some point.
 * Key is not used
 */
public class RowOutputFormat
    implements org.apache.hadoop.mapred.OutputFormat<NullWritable, Row> {
  private static final Log log = LogFactory.getLog(RowOutputFormat.class);

  public static final String NAMESPACE = "hypertable.mapred.output.namespace";
  public static final String TABLE = "hypertable.mapred.output.table";

  public static final String MUTATOR_FLAGS = "hypertable.mapred.output.mutator-flags";
  public static final String BUFFER_SIZE = "hypertable.mapred.output.buffer-size";
  public static final String MUTATOR_FLUSH_INTERVAL = "hypertable.mapred.output.mutator-flush-interval";
  public static final int msDefaultSerializedCellBufferSize=1000000; // 1M default buffer

  /**
   * Write reducer output to HT via Thrift interface
   *
   */
  protected static class HypertableRecordWriter implements RecordWriter<NullWritable, Row> {
    private ThriftClient mClient;
    private long mMutator;
    private long mNamespace;
    private String namespace;
    private String table;
    private SerializedCellsWriter mSerializedCellsWriter;

    /**
     * Opens a client & mutator to specified table
     *
     * @param namespace Namespace which contains the HT Table
     * @param table name of HT table
     * @param flags mutator flags
     * @param flush_interval used for periodic flush mutators
     * @param buffer_size buffer up cells to this size limit
     * @param framesize max thrift framesize
     */
    public HypertableRecordWriter(String namespace, String table, int flags,
            int flush_interval, int buffer_size, int framesize)
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
        mSerializedCellsWriter = new SerializedCellsWriter(buffer_size, false);
      }
      catch (Exception e) {
        log.error(e);
        throw new IOException("Unable to open thrift mutator - " + e.toString());
      }
    }

    /**
     * Ctor with default flags=NO_LOG_SYNC and flush interval set to 0
     */
    public HypertableRecordWriter(String namespace, String table) throws IOException {
      this(namespace, table, MutatorFlag.NO_LOG_SYNC.getValue(), 0,
              msDefaultSerializedCellBufferSize, 0);
    }

    /**
     * Ctor with default flush interval set to 0
     */
    public HypertableRecordWriter(String namespace, String table, int flags) throws IOException {
      this(namespace, table, flags, 0, msDefaultSerializedCellBufferSize, 0);
    }

    /**
     * Close mutator and client
     * @param reporter
     */
    public void close(Reporter reporter) throws IOException {
      try {
        // Flush remaining buffer to ThriftBroker
        if (!mSerializedCellsWriter.isEmpty()) {
          mClient.mutator_set_cells_serialized(mMutator, mSerializedCellsWriter.buffer(), true);
        }

        mClient.close_mutator(mMutator);
        mClient.close_namespace(mNamespace);
      }
      catch (Exception e) {
        log.error(e);
        throw new IOException("Unable to close thrift mutator & namespace- " + e.toString());
      }
    }

    /**
     * Write data to HT
     */
    public void write(NullWritable key, Row value) throws IOException {
      try {
        byte [] cells = value.getSerializedRow();
        boolean added = mSerializedCellsWriter.add_serialized_cell_array(cells);

        // if buffer is full flush to ThriftBroker and clear
        if (!added) {
          mClient.mutator_set_cells_serialized(mMutator, mSerializedCellsWriter.buffer(), false);

          // this Row is larger than the buffer, increase buffer size
          if (cells.length > mSerializedCellsWriter.capacity())
            mSerializedCellsWriter = new SerializedCellsWriter((cells.length*3)/2, false);
          else
            mSerializedCellsWriter.clear();
          added = mSerializedCellsWriter.add_serialized_cell_array(cells);
          if (!added) {
            throw new IOException("Unable to add cell array of size " + cells.length
                + " to SerializedCellsWriter of capacity "
                + mSerializedCellsWriter.capacity());
          }
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
  public RecordWriter<NullWritable, Row> getRecordWriter(FileSystem ignored, JobConf job, String name, Progressable progress)
    throws IOException {

    String namespace = job.get(RowOutputFormat.NAMESPACE);
    String table = job.get(RowOutputFormat.TABLE);
    int flags = job.getInt(RowOutputFormat.MUTATOR_FLAGS, 0);
    int flush_interval = job.getInt(RowOutputFormat.MUTATOR_FLUSH_INTERVAL, 0);
    int buffer_size = job.getInt(RowOutputFormat.BUFFER_SIZE,
                                 msDefaultSerializedCellBufferSize);

    try {
      return new HypertableRecordWriter(namespace, table, flags,
              flush_interval, buffer_size, 0);
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
    public void checkOutputSpecs(FileSystem ignore, JobConf conf)
      throws IOException {
    try {
      //if !(mClient.exists_table();
    }
    catch (Exception e) {
      log.error(e);
      throw new IOException("Unable to get table id - " + e.toString());
    }
  }

}

