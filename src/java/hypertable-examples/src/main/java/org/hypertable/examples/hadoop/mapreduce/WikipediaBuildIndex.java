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
import java.io.UnsupportedEncodingException;

import java.lang.Character;
import java.text.ParseException;
import java.util.Date;
import java.util.ArrayList;
import java.util.StringTokenizer;

import org.apache.hadoop.conf.Configuration;
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

import org.hypertable.hadoop.mapreduce.Helper;
import org.hypertable.hadoop.mapreduce.KeyWritable;
import org.hypertable.hadoop.mapreduce.ScanSpec;

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
 * hadoop jar $JARFILE org/hypertable/examples/WikipediaBuildIndex --columns=article
 */
public class WikipediaBuildIndex {

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
	String article = key.getRow();
	String rowKey;
	StringTokenizer st = null;

        try {
          st = new StringTokenizer( new String(value.getBytes(), "UTF-8") );
        }
        catch (UnsupportedEncodingException e) {
          e.printStackTrace();
          System.exit(-1);
        }

	while (st.hasMoreTokens()) {
	    rowKey = st.nextToken();
	    key.setRow(rowKey);
	    key.setColumn_family("article");
	    key.setColumn_qualifier(article);
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
    public ScanSpec scan_spec = new ScanSpec();
  }

  /**
   * Parses the command line arguments and returns a populated Arguments
   * object.
   */
  public static Arguments parseArgs(String[] args) throws ParseException {
    Arguments parsed_args = new Arguments();
    Date ts;
    for (int i=0; i<args.length; i++) {
      if (args[i].startsWith("--start-time=")) {
        ts = Time.parse_ts(args[i].substring(13));
        parsed_args.scan_spec.start_time = ts.getTime() * 1000000;
        parsed_args.scan_spec.setStart_timeIsSet(true);
      }
      else if (args[i].startsWith("--end-time=")) {
        ts = Time.parse_ts(args[i].substring(11));
        parsed_args.scan_spec.end_time = ts.getTime() * 1000000;
        parsed_args.scan_spec.setEnd_timeIsSet(true);
      }
    }
    return parsed_args;
  }


  /**
   * Main entry point for WikipediaBuildIndex example.
   *
   * @param args  The command line parameters.
   * @throws Exception When running the job fails.
   */
  public static void main(String[] args) throws Exception {
    Arguments parsed_args = parseArgs(args);
    Configuration conf = new Configuration();
    String[] otherArgs = new GenericOptionsParser(conf, args).getRemainingArgs();
    Job job = Job.getInstance(conf, "wikipedia");
    job.setJarByClass(WikipediaBuildIndex.class);
    job.setCombinerClass(IntSumReducer.class);

    parsed_args.scan_spec.addToColumns("article");

    /**
     * Sets up the Mapper configuration.  Specifies the mapper class, the input
     * format class (org.hypertable.hadoop.mapreduce.InputFormat) the input
     * table name ("wikipedia") and the mapper's output types
     * (KeyWritable, IntWritable).  This method will also serialize the scan
     * specification into the job configuration which is used in the InputFormat
     * as the scanner predicate.
     */
    Helper.initMapperJob("/", "wikipedia", parsed_args.scan_spec, TokenizerMapper.class,
                         KeyWritable.class, IntWritable.class, job);

    /**
     * Sets up the Reducer configuration.  Specifies the reducer class, the
     * output format class (org.hypertable.hadoop.mapreduce.InputFormat), the
     * output table name ("wikiindex") and the reducer's output types to the
     * defaults (KeyWritable, BytesWritable).
     */
    Helper.initReducerJob("/", "wikiindex", IntSumTableReducer.class, job);

    System.exit(job.waitForCompletion(true) ? 0 : 1);
  }
}
