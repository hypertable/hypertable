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

package org.hypertable.examples;

import java.io.IOException;
import java.io.DataInput;
import java.io.DataOutput;
import java.util.ArrayList;
import java.util.List;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.apache.hadoop.mapreduce.InputFormat;
import org.apache.hadoop.mapreduce.InputSplit;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.JobContext;
import org.apache.hadoop.mapreduce.Mapper;
import org.apache.hadoop.mapreduce.RecordReader;
import org.apache.hadoop.mapreduce.Reducer;
import org.apache.hadoop.mapreduce.TaskAttemptContext;

import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.io.NullWritable;
import org.apache.hadoop.io.Writable;

import org.hypertable.thrift.ThriftClient;
import org.hypertable.thriftgen.*;
import org.hypertable.hadoop.mapreduce.*;


/**
 * Load Test.
 * Each mapper generates N rows (cells) starting at StartRow.
 * Mapper:
 *   KeyIn=null, ValueIn=Split definition (start row & num rows to generate)
 *   KeyOut=KeyWritable, ValueOut=BytesWritable
 * Reducer:
 *   KeyIn=KeyWritable, ValueIn=BytesWritable
 *   KeyOut=null, ValueOut=null
 * Reducer simply inserts cells into HT
 */
public class LoadTest {
  private static final Log log = LogFactory.getLog(LoadTest.class);

  public LoadTest() {
    this.clients=1;
    this.totalRows=0;
  }

  public static class LoadSplit extends InputSplit implements Writable {
    private int startRow = 0;
    private int numRows = 0;
    private int splitId = 0;

    public LoadSplit() {
      this.startRow = 0;
      this.numRows = 0;
      this.splitId = 0;
    }

    public LoadSplit(int startRow, int numRows, int splitId) {
      this.startRow = startRow;
      this.numRows = numRows;
      this.splitId = splitId;
    }

    @Override
    public void readFields(DataInput in) throws IOException {
      this.startRow = in.readInt();
      this.numRows = in.readInt();
      this.splitId = in.readInt();
    }

    @Override
    public void write(DataOutput out) throws IOException {
      out.writeInt(startRow);
      out.writeInt(numRows);
      out.writeInt(splitId);
    }

    @Override
    public String[] getLocations() throws IOException, InterruptedException {
      return new String[0];
    }

    @Override
    public long getLength() throws IOException, InterruptedException {
      return 0;
    }

    public int getStartRow() {
      return startRow;
    }

    public int getNumRows() {
      return numRows;
    }

    public int getSplitId() {
      return splitId;
    }

  }

  public static class LoadInputFormat extends InputFormat<NullWritable, LoadSplit> {
    @Override
    public List<InputSplit> getSplits(JobContext job) throws IOException {
      List<InputSplit> splitList = new ArrayList<InputSplit>();
      int totalRows = job.getConfiguration().getInt("LoadSplit.TOTAL_ROWS", 0);
      int clients = job.getConfiguration().getInt("LoadSplit.CLIENTS", 1);
      int numRows = totalRows/clients;

      for(int ii=0; ii<clients; ++ii) {
        int startRow = ii*numRows;
        LoadSplit split = new LoadSplit(startRow, numRows, ii);
        splitList.add(split);
      }
      return splitList;
    }

    @Override
    public RecordReader<NullWritable, LoadSplit> createRecordReader(InputSplit split,
                TaskAttemptContext context) {
      return new LoadRecordReader();
    }
  }

  /**
   * For every input split the record reader returns only one key value pair.
   * The key is nullwritable and the value is the split itself.
   */
  public static class LoadRecordReader extends RecordReader<NullWritable, LoadSplit> {
      private LoadSplit split = null;
      private NullWritable key = null;
      private LoadSplit value = null;
      private boolean finished = false;

      @Override
      public void initialize(InputSplit split, TaskAttemptContext context)
            throws IOException, InterruptedException {
        this.split = (LoadSplit) split;
        this.finished = false;
      }

      @Override
      public boolean nextKeyValue() {
        if (finished)
          return false;

        key = NullWritable.get();
        value = (LoadSplit)split;
        finished = true;
        return true;
      }

      @Override
      public NullWritable getCurrentKey() {
        return key;
      }

      @Override
      public LoadSplit getCurrentValue() {
        return value;
      }

      @Override
      public float getProgress() {
        if (finished)
          return 1.0f;
        return 0.0f;
      }

      @Override
      public void close() {
        // do nothing
      }
    }

  public static class LoadMapper extends Mapper<NullWritable, LoadSplit, KeyWritable, BytesWritable> {

    static enum Counters {NUM_CELLS};

    public void map(NullWritable key, LoadSplit value, final Context ctx)
        throws IOException, InterruptedException {
      int startRow = value.getStartRow();
      int endRow =  value.getNumRows() + startRow ;
      int id = value.getSplitId();

      for (int ii=startRow; ii < endRow; ++ii) {
        KeyWritable output_key = new KeyWritable();
        BytesWritable output_value = new BytesWritable(Integer.toString(ii).getBytes());
        output_key.row = Integer.toString(ii);
        output_key.column_family = "col";
        ctx.write(output_key, output_value);
        ctx.getCounter(Counters.NUM_CELLS).increment(1);
        log.info("Mapper" + Integer.toString(id) + "created cell " + Integer.toString(ii));
      }
    }
  }

  public static class LoadReducer extends Reducer<KeyWritable, BytesWritable, KeyWritable, BytesWritable> {
    static enum Counters {NUM_CELLS, ELAPSED_TIME_MS};
    public void reduce(KeyWritable key, Iterable<BytesWritable> values, Context ctx)
      throws IOException, InterruptedException {
      for(BytesWritable value : values) {
        long startTime = System.currentTimeMillis();
        ctx.write(key, value);
        long elapsedTime = System.currentTimeMillis() - startTime;
        ctx.getCounter(Counters.ELAPSED_TIME_MS).increment(elapsedTime);
        ctx.getCounter(Counters.NUM_CELLS).increment(1);
      }
    }
  }

  private void doMapReduce() {
    try {
      Job job = Job.getInstance();

      job.getConfiguration().set(OutputFormat.NAMESPACE, "/");
      job.getConfiguration().set(OutputFormat.TABLE, "LoadTest");
      job.getConfiguration().setInt(OutputFormat.MUTATOR_FLAGS,
                                    MutatorFlag.NO_LOG_SYNC.getValue());
      job.getConfiguration().setInt(OutputFormat.MUTATOR_FLUSH_INTERVAL, 0);
      job.getConfiguration().setInt("LoadSplit.TOTAL_ROWS", this.totalRows);
      job.getConfiguration().setInt("LoadSplit.CLIENTS", this.clients);
      job.setJarByClass(LoadTest.class);
      job.setJobName("Hypertable MapReduce connector LoadTest");
      job.setInputFormatClass(LoadInputFormat.class);
      job.setOutputFormatClass(OutputFormat.class);
      job.setMapOutputKeyClass(KeyWritable.class);
      job.setMapOutputValueClass(BytesWritable.class);
      job.setMapperClass(LoadMapper.class);
      job.setReducerClass(LoadReducer.class);
      job.setNumReduceTasks(this.clients);

      job.waitForCompletion(true);
    }
    catch (Exception e) {
      e.printStackTrace();
    }
  }

  private int run(String[] args) {

    String rows = "--rows=";
    String clients = "--clients=";
    try {
      for(int ii=0; ii < args.length; ++ii) {
        String cmd = args[ii];
        if (cmd.startsWith(rows)) {
          this.totalRows = Integer.parseInt(cmd.substring(rows.length()));
        }
        else if (cmd.startsWith(clients)) {
          this.clients = Integer.parseInt(cmd.substring(clients.length()));
        }
        else {
          printUsage();
          return -1;
        }
      }
      doMapReduce();
    }
    catch (Exception e) {
      e.printStackTrace();
    }
    return 0;
  }

  private void printUsage() {
    System.err.println("Usage: java " + this.getClass().getName() + " --rows=N");
  }

  public static void main(String[] args) throws Exception {

    LoadTest test = new LoadTest();
    System.exit(test.run(args));
  }

  private int totalRows;
  private int clients;
}
