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
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.text.ParseException;
import java.util.Random;
import java.util.Vector;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.io.Text;

import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.util.Progressable;

import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.mapred.RecordWriter;
import org.apache.hadoop.mapred.Reporter;
import org.apache.hadoop.mapred.JobConfigurable;

import org.hypertable.Common.HostSpecification;
import org.hypertable.thriftgen.*;
import org.hypertable.thrift.ThriftClient;
import org.hypertable.thrift.SerializedCellsFlag;
import org.hypertable.thrift.SerializedCellsWriter;

/**
 * Write Map/Reduce output to a table in Hypertable.
 *
 * TODO: For now we assume ThriftBroker is running on localhost on default
 * port (15867). Change this to read from configs at some point.
 */
public class TextTableOutputFormat
    implements org.apache.hadoop.mapred.OutputFormat<Text, Text> {
  private static final Log log = LogFactory.getLog(TextTableOutputFormat.class);

  public static final String NAMESPACE = "hypertable.mapreduce.namespace";
  public static final String OUTPUT_NAMESPACE = "hypertable.mapreduce.output.namespace";
  public static final String TABLE = "hypertable.mapreduce.output.table";
  public static final String MUTATOR_FLAGS = "hypertable.mapreduce.output.mutator_flags";
  public static final String MUTATOR_FLUSH_INTERVAL = "hypertable.mapreduce.output.mutator_flush_interval";
  public static final String THRIFT_FRAMESIZE  = "hypertable.mapreduce.thriftbroker.framesize";
  public static final String THRIFT_FRAMESIZE2 = "hypertable.mapreduce.thriftclient.framesize";
  public static final String THRIFT_HOST       = "hypertable.mapreduce.thriftbroker.host";
  public static final String THRIFT_PORT       = "hypertable.mapreduce.thriftbroker.port";

  /**
   * Gets the ThriftBroker host name.
   * Obtains the ThriftBroker host name from the
   * "hypertable.mapreduce.thriftbroker.host" property.  The value of this
   * property can either be a host name or IP address, or it can be a host
   * specification.  If it is a host specification, it is expanded into a vector
   * of host names, one of which is chosen at random.  The default value of
   * "localhost" is used if the host property was not supplied.
   * @param job Job configuration
   * @return Host name of ThriftBroker
   */
  private static String getThriftHost(JobConf job) throws ParseException {
    String host = "localhost";
    String spec = job.get(THRIFT_HOST);
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
   * Create a record writer
   */
  public RecordWriter<Text, Text> getRecordWriter(FileSystem ignored,
          JobConf job, String name, Progressable progress)
    throws IOException {
    String namespace = job.get(TextTableOutputFormat.OUTPUT_NAMESPACE);
    if (namespace == null)
      namespace = job.get(TextTableOutputFormat.NAMESPACE);
    String table = job.get(TextTableOutputFormat.TABLE);
    int flags = job.getInt(TextTableOutputFormat.MUTATOR_FLAGS, 0);
    int flush_interval = job.getInt(TextTableOutputFormat.MUTATOR_FLUSH_INTERVAL, 0);
    int framesize = job.getInt(TextTableOutputFormat.THRIFT_FRAMESIZE, 0);
    if (framesize == 0)
      framesize = job.getInt(TextTableOutputFormat.THRIFT_FRAMESIZE2, 0);

    try {
      String host = getThriftHost(job);
      int port = job.getInt(THRIFT_PORT, 15867);
      ThriftClient client;
      if (framesize != 0)
        client = ThriftClient.create(host, port, 1600000, true, framesize);
      else
        client = ThriftClient.create(host, port);
      return new HypertableRecordWriter(client, namespace, table, flags,
                                        flush_interval);
    }
    catch (Exception e) {
      log.error(e);
      throw new IOException("Unable to access RecordWriter - " + e.toString());
    }
  }

  /**
   * TODO: Do something meaningful here
   * Make sure the table exists
   */
    public void checkOutputSpecs(FileSystem ignore, JobConf conf)
      throws IOException {
    try {
      //mClient.table_get_id();
    }
    catch (Exception e) {
      log.error(e);
      throw new IOException("Unable to get table id - " + e.toString());
    }
  }
}

