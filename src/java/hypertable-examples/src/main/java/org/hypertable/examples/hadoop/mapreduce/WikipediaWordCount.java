/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither Hypertable, Inc. nor the names of its contributors may be used to
 *   endorse or promote products derived from this software without specific
 *   prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

package org.hypertable.examples;

import java.io.IOException;
import java.lang.Character;
import java.text.ParseException;
import java.util.Date;
import java.util.ArrayList;
import java.util.StringTokenizer;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.conf.Configured;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.io.IntWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.Mapper;
import org.apache.hadoop.mapreduce.Reducer;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;
import org.apache.hadoop.util.GenericOptionsParser;
import org.apache.hadoop.util.Tool;
import org.apache.hadoop.util.ToolRunner;

import org.hypertable.hadoop.mapreduce.Helper;
import org.hypertable.hadoop.mapreduce.KeyWritable;
import org.hypertable.hadoop.mapreduce.ScanSpec;

import org.hypertable.thriftgen.RowInterval;

import org.hypertable.Common.Time;

/**
 * Hypertable MapReduce example program.
 *
 * This program is designed to parse the "article" column and poplulate
 * the "word" column.  Each unique word in the article will get written
 * as a qualified column (the unique word is the qualifier) and the value
 * is a count of how many times that word occurred in the article.  The
 * program is meant to be run as follows:
 *
 * hadoop jar $JARFILE org/hypertable/examples/WikipediaWordCount --columns=article
 */
public class WikipediaWordCount extends Configured implements Tool {

  /**
   * Simple ASCII Tokenizer class.  This class is used to extract words
   * (ASCII characters only) from a byte array.  The reason for using this
   * class instead of StringTokenizer is because the StringTokenizer class
   * was too inefficient (object construction and garbage collection).
   * This class also allows us to use the (buffer,offset,length) methods of
   * the KeyWritable class which are much more efficient than the String
   * counterparts.
   */
  public static class SimpleASCIITokenizer {

    /**
     * Resets the tokenizer with a new byte array
     *
     * @param utf8str byte array holding text
     */
    void reset(byte [] utf8str) {
      m_buffer = utf8str;
      m_offset = 0;
      m_length = 0;
    }

    /**
     * Extracts the next word from the byte array.  If the extraction was
     * successful (e.g. didn't hit EOF), the m_offset and m_length members
     * will be set up to indicate the position of the word in the byte array.
     *
     * @return true if word successfully parsed, false if EOF
     */
    boolean next() {
      m_offset += m_length;

      if (m_offset == m_buffer.length)
        return false;

      if (m_offset > 0)
        m_offset++;

      while (m_offset < m_buffer.length) {
        if (m_buffer[m_offset] > 0) {
          if (Character.isLetterOrDigit((char)m_buffer[m_offset]))
            break;
        }
        m_offset++;
      }
      if (m_offset == m_buffer.length)
        return false;

      m_length = 0;
      while (m_offset+m_length < m_buffer.length &&
             m_buffer[m_offset+m_length] > 0 &&
             Character.isLetterOrDigit((char)m_buffer[m_offset+m_length]))
        m_length++;

      return true;
    }

    /**
     * Returns offset of word in byte array after call to next()
     *
     * @return offset of word in byte array
     */
    int getOffset() { return m_offset; }

    /**
     * Returns length of word in byte array after call to next()
     *
     * @return length of word in byte array
     */
    int getLength() { return m_length; }

    private byte [] m_buffer;
    private int m_offset;
    private int m_length;
  }

  /**
   * Mapper class.  This class receives a key/value pair from Hypertable
   * and tokenizes the value by splitting it into words and outputting a
   * new KeyWritable with column family "word" and column qualifier is the
   * actual word itself.  The value is an IntWritable(1).
   */
  public static class TokenizerMapper
       extends org.hypertable.hadoop.mapreduce.Mapper<KeyWritable, IntWritable>{

    private final static IntWritable one = new IntWritable(1);

    public void map(KeyWritable key, BytesWritable value, Context context
                    ) throws IOException, InterruptedException {
      SimpleASCIITokenizer tokenizer = new SimpleASCIITokenizer();
      byte [] bytes = value.getBytes();

      tokenizer.reset(bytes);
      key.setColumn_family("word");

      while (tokenizer.next()) {
        key.setColumn_qualifier(bytes, tokenizer.getOffset(), tokenizer.getLength());
        context.write(key, one);
      }
      context.progress();
    }
  }

  /**
   * Reducer class used for the combiner.  Sums up the occurrences of each
   * unique word in each article.
   */
  public static class IntSumReducer
       extends Reducer<KeyWritable,IntWritable,KeyWritable,IntWritable> {
    private IntWritable result = new IntWritable();

    public void reduce(KeyWritable key, Iterable<IntWritable> values,
                       Context context
                       ) throws IOException, InterruptedException {
      int sum = 0;
      for (IntWritable val : values) {
        sum += val.get();
      }
      result.set(sum);
      context.write(key, result);
      context.progress();
    }
  }

  /**
   * Reducer class.  Sums up the occurrences of each unique word in each
   * article.
   */
  public static class IntSumTableReducer
       extends org.hypertable.hadoop.mapreduce.Reducer<KeyWritable, IntWritable>{

    public void reduce(KeyWritable key, Iterable<IntWritable> values,
                       Context context
                       ) throws IOException, InterruptedException {
      int sum = 0;
      for (IntWritable val : values) {
        sum += val.get();
      }
      byte [] vbytes = String.valueOf(sum).getBytes();
      BytesWritable output_value = new BytesWritable(vbytes);
      context.write(key, output_value);
      context.progress();
    }
  }

  /**
   * Class to hold the parsed command line arguments.  Currently just
   * holds the scan specification (scan predicate).
   */
  public static class Arguments {
    public String namespace = "/";
    public ScanSpec scan_spec = new ScanSpec();
  }

  /**
   * Parses the command line arguments and returns a populated Arguments
   * object.
   */
  public static Arguments parseArgs(String[] args) throws ParseException {
    Arguments parsed_args = new Arguments();
    RowInterval row_interval = null;
    Date ts;
    for (int i=0; i<args.length; i++) {
      if (args[i].startsWith("--namespace=")) {
        parsed_args.namespace = args[i].substring(12);
      }
      else if (args[i].startsWith("--row-")) {
        if (row_interval == null)
          row_interval = new RowInterval();
        if (args[i].startsWith("--row-gt=")) {
          row_interval.setStart_row(args[i].substring(9));
          row_interval.setStart_inclusive(false);
        }
        else if (args[i].startsWith("--row-ge=")) {
          row_interval.setStart_row(args[i].substring(9));
          row_interval.setStart_inclusive(true);
        }
        else if (args[i].startsWith("--row-lt=")) {
          row_interval.setEnd_row(args[i].substring(9));
          row_interval.setEnd_inclusive(false);
        }
        else if (args[i].startsWith("--row-le=")) {
          row_interval.setEnd_row(args[i].substring(9));
          row_interval.setEnd_inclusive(true);
        }
        else {
          System.out.println("Unsupported option: " + args[i]);
          System.exit(-1);
        }
      }
      else if (args[i].startsWith("--start-time=")) {
        ts = Time.parse_ts(args[i].substring(13));
        parsed_args.scan_spec.start_time = ts.getTime() * 1000000;
        parsed_args.scan_spec.setStart_timeIsSet(true);
      }
      else if (args[i].startsWith("--end-time=")) {
        ts = Time.parse_ts(args[i].substring(11));
        parsed_args.scan_spec.end_time = ts.getTime() * 1000000;
        parsed_args.scan_spec.setEnd_timeIsSet(true);
      }
      else if (args[i].startsWith("--columns=")) {
        StringTokenizer itr = new StringTokenizer(args[i].substring(10), ",");
        ArrayList<String> columns = new ArrayList<String>();
        while (itr.hasMoreTokens())
          columns.add(itr.nextToken());
        parsed_args.scan_spec.setColumns(columns);
        parsed_args.scan_spec.setColumnsIsSet(true);
      }
      else {
        System.out.println("Unsupported option: " + args[i]);
        System.exit(-1);
      }
    }
    if (row_interval != null) {
      parsed_args.scan_spec.addToRow_intervals(row_interval);
    }
    return parsed_args;
  }


  /**
   * Main entry point for WikipediaWordCount example.
   *
   * @param args  The command line parameters.
   * @throws Exception When running the job fails.
   */
  public int run(String[] args) throws Exception {
    Job job = Job.getInstance(getConf());
    job.setJarByClass(WikipediaWordCount.class);
    job.setJobName("wordcount");
    Arguments parsed_args = parseArgs(args);

    job.setCombinerClass(IntSumReducer.class);

    /**
     * Sets up the Mapper configuration.  Specifies the mapper class, the input
     * format class (org.hypertable.hadoop.mapreduce.InputFormat) the input
     * table name ("wikipedia") and the mapper's output types
     * (KeyWritable, IntWritable).  This method will also serialize the scan
     * specification into the job configuration which is used in the InputFormat
     * as the scanner predicate.
     */
    Helper.initMapperJob(parsed_args.namespace, "wikipedia", parsed_args.scan_spec,
                         TokenizerMapper.class, KeyWritable.class, IntWritable.class, job);

    /**
     * Sets up the Reducer configuration.  Specifies the reducer class, the
     * output format class (org.hypertable.hadoop.mapreduce.InputFormat), the
     * output table name ("wikipedia") and the reducer's output types to the
     * defaults (KeyWritable, BytesWritable).
     */
    Helper.initReducerJob(parsed_args.namespace, "wikipedia", IntSumTableReducer.class, job);

    boolean success = job.waitForCompletion(true);
    return success ? 0 : 1;
  }

  public static void main(String[] args) throws Exception {
    int res = ToolRunner.run(new Configuration(), new WikipediaWordCount(), args);
    System.exit(res);
  }
}
