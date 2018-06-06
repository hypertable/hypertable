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
import java.io.UnsupportedEncodingException;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.Charset;
import java.nio.charset.CharsetEncoder;

import java.text.ParseException;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.Vector;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.mapreduce.InputSplit;
import org.apache.hadoop.mapreduce.JobContext;
import org.apache.hadoop.mapreduce.TaskAttemptContext;

import org.hypertable.Common.HostSpecification;
import org.hypertable.thriftgen.*;
import org.hypertable.thrift.ThriftClient;

import org.apache.thrift.transport.TTransportException;
import org.apache.thrift.TException;


public class InputFormat
extends org.apache.hadoop.mapreduce.InputFormat<KeyWritable, BytesWritable> {

  final Log LOG = LogFactory.getLog(InputFormat.class);

  public static final String NAMESPACE = "hypertable.mapreduce.input.namespace";
  public static final String TABLE = "hypertable.mapreduce.input.table";
  public static final String SCAN_SPEC = "hypertable.mapreduce.input.scan-spec";
  public static final String THRIFT_FRAMESIZE = "hypertable.mapreduce.thriftclient.framesize";
  public static final String THRIFT_FRAMESIZE2 = "hypertable.mapreduce.thriftbroker.framesize";
  public static final String THRIFT_HOST = "hypertable.mapreduce.thriftbroker.host";
  public static final String THRIFT_PORT = "hypertable.mapreduce.thriftbroker.port";

  private ThriftClient m_client = null;
  private ScanSpec m_base_spec = null;
  private String m_tablename = null;
  private String m_namespace = null;

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
  private String getThriftHost(Configuration conf) throws ParseException {
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
   * Gets the ThriftBroker framesize.
   * Obtains the framesize by first reading the 
   * "hypertable.mapreduce.thriftbroker.framesize" property and if that is zero,
   * reads the deprecated "hypertable.mapreduce.thriftclient.framesize"
   * @param conf Job configuration
   * @return ThriftBroker frame size
   */
  private int getThriftFramesize(Configuration conf) {
    int framesize = conf.getInt(THRIFT_FRAMESIZE, 0);
    if (framesize == 0)
      framesize = conf.getInt(THRIFT_FRAMESIZE2, 0);
    return framesize;
  }


  protected class RecordReader
  extends org.apache.hadoop.mapreduce.RecordReader<KeyWritable, BytesWritable> {

    private ThriftClient m_client = null;
    private long m_ns = 0;
    private long m_scanner = 0;
    private ScanSpec m_scan_spec = null;
    private String m_namespace = null;
    private String m_tablename = null;
    private long m_bytes_read = 0;

    private List<Cell> m_cells = null;
    private Iterator<Cell> m_iter = null;
    private boolean m_eos = false;

    private KeyWritable m_key = new KeyWritable();
    private BytesWritable m_value = null;

    /**
     *  Constructor
     *
     * @param client Hypertable Thrift client
     * @param namespace name of HT namespace
     * @param tablename name of table to read from
     * @param scan_spec scan specification
     */
    public RecordReader(ThriftClient client, String namespace, String tablename, ScanSpec scan_spec) {
      m_client = client;
      m_namespace = namespace;
      m_tablename = tablename;
      m_scan_spec = scan_spec;
    }

    /**
     * Initializes the reader.
     *
     * @param inputsplit  The split to work with.
     * @param context  The current task context.
     * @throws IOException When setting up the reader fails.
     * @throws InterruptedException When the job is aborted.
     * @see org.apache.hadoop.mapreduce.RecordReader#initialize(
     *   org.apache.hadoop.mapreduce.InputSplit,
     *   org.apache.hadoop.mapreduce.TaskAttemptContext)
     */
    @Override
    public void initialize(InputSplit inputsplit,
        TaskAttemptContext context) throws IOException,
        InterruptedException {
      try {
        m_ns = m_client.namespace_open(m_namespace);
        m_scanner = m_client.scanner_open(m_ns, m_tablename, m_scan_spec);
      }
      catch (TTransportException e) {
        e.printStackTrace();
        throw new IOException(e.getMessage());
      }
      catch (TException e) {
        e.printStackTrace();
        throw new IOException(e.getMessage());
      }
    }

    /**
     * Closes the split.
     *
     * @see org.apache.hadoop.mapreduce.RecordReader#close()
     */
    @Override
    public void close() {
      try {
        m_client.scanner_close(m_scanner);
        m_client.namespace_close(m_ns);
      }
      catch (Exception e) {
        e.printStackTrace();
      }
    }

    /**
     * The current progress of the record reader through its data.
     *
     * @return A number between 0.0 and 1.0, the fraction of the data read.
     * @see org.apache.hadoop.mapreduce.RecordReader#getProgress()
     */
    @Override
    public float getProgress() {
      // Assume 200M split size
      if (m_bytes_read >= 200000000)
        return (float)1.0;
      return (float)m_bytes_read / (float)200000000.0;
    }

    /**
     * Returns the current key.
     *
     * @return The current key.
     * @throws IOException
     * @throws InterruptedException When the job is aborted.
     * @see org.apache.hadoop.mapreduce.RecordReader#getCurrentKey()
     */
    @Override
    public KeyWritable getCurrentKey() throws IOException,
        InterruptedException {
      return m_key;
    }

    /**
     * Returns the current value.
     *
     * @return The current value.
     * @throws IOException When the value is faulty.
     * @throws InterruptedException When the job is aborted.
     * @see org.apache.hadoop.mapreduce.RecordReader#getCurrentValue()
     */
    @Override
    public BytesWritable getCurrentValue() throws IOException, InterruptedException {
      return m_value;
    }

    /**
     * Positions the record reader to the next record.
     *
     * @return <code>true</code> if there was another record.
     * @throws IOException When reading the record failed.
     * @throws InterruptedException When the job was aborted.
     * @see org.apache.hadoop.mapreduce.RecordReader#nextKeyValue()
     */
    @Override
    public boolean nextKeyValue() throws IOException, InterruptedException {
      try {
        if (m_eos)
          return false;
        if (m_cells == null || !m_iter.hasNext()) {
          m_cells = m_client.scanner_get_cells(m_scanner);
          if (m_cells.isEmpty()) {
            m_eos = true;
            return false;
          }
          m_iter = m_cells.iterator();
        }
        Cell cell = m_iter.next();
        m_key.load(cell.key);
        byte [] value = new byte[0];
        m_bytes_read += 24 + cell.key.row.length();
        if (cell.value != null && cell.value.hasRemaining()) {
          value = new byte [ cell.value.remaining() ];
          System.arraycopy(cell.value.array(),
                  cell.value.arrayOffset()+cell.value.position(),
                  value, 0, value.length);
        }
        m_value = new BytesWritable(value);
        m_bytes_read += 24 + cell.key.row.length() + value.length;
        if (cell.key.column_qualifier != null)
          m_bytes_read += cell.key.column_qualifier.length();
      }
      catch (TTransportException e) {
        e.printStackTrace();
        throw new IOException(e.getMessage());
      }
      catch (TException e) {
        e.printStackTrace();
        throw new IOException(e.getMessage());
      }
      return true;
    }

  }

  /**
   * Builds a RecordReader.
   *
   * @param split  The split to work with.
   * @param context  The current context.
   * @return The newly created record reader.
   * @throws IOException When creating the reader fails.
   * @see org.apache.hadoop.mapreduce.InputFormat#createRecordReader(
   *   org.apache.hadoop.mapreduce.InputSplit,
   *   org.apache.hadoop.mapreduce.TaskAttemptContext)
   */
  @Override
  public org.apache.hadoop.mapreduce.RecordReader<KeyWritable, BytesWritable>
    createRecordReader(InputSplit split, TaskAttemptContext context) throws IOException {
    try {
      TableSplit ts = (TableSplit)split;

      if (m_namespace == null)
        m_namespace = context.getConfiguration().get(NAMESPACE);

      if (m_tablename == null) {
        m_tablename = context.getConfiguration().get(TABLE);
        m_base_spec = ScanSpec.serializedTextToScanSpec(context.getConfiguration().get(SCAN_SPEC));
        System.err.println(m_base_spec);
      }

      ScanSpec scan_spec = ts.createScanSpec(m_base_spec);
      System.err.println(scan_spec);

      if (m_client == null) {
        String host = getThriftHost(context.getConfiguration());
        int port = context.getConfiguration().getInt(THRIFT_PORT, 15867);
        int framesize = getThriftFramesize(context.getConfiguration());
        if (framesize != 0)
          m_client = ThriftClient.create(host, port, 1600000, true, framesize);
        else
          m_client = ThriftClient.create(host, port);
      }
      return new RecordReader(m_client, m_namespace, m_tablename, scan_spec);
    }
    catch (TTransportException e) {
      e.printStackTrace();
      throw new IOException(e.getMessage());
    }
    catch (TException e) {
      e.printStackTrace();
      throw new IOException(e.getMessage());
    }
    catch (ParseException e) {
      e.printStackTrace();
      throw new IOException(e.getMessage());
    }
  }

  /**
   * Calculates the splits that will serve as input for the map tasks. The
   * number of splits matches the number of ranges in a table.
   *
   * @param context  The current job context.
   * @return The list of input splits.
   * @throws IOException When creating the list of splits fails.
   * @see org.apache.hadoop.mapreduce.InputFormat#getSplits(
   *   org.apache.hadoop.mapreduce.JobContext)
   */
  @Override
  public List<InputSplit> getSplits(JobContext context) throws IOException {

    long ns=0;
    try {
      RowInterval ri = null;

      if (m_client == null) {
        String host = getThriftHost(context.getConfiguration());
        int port = context.getConfiguration().getInt(THRIFT_PORT, 15867);
        int framesize = getThriftFramesize(context.getConfiguration());
        if (framesize != 0)
          m_client = ThriftClient.create(host, port, 1600000, true, framesize);
        else
          m_client = ThriftClient.create(host, port);
      }

      if (m_base_spec == null)
        m_base_spec = ScanSpec.serializedTextToScanSpec(context.getConfiguration().get(SCAN_SPEC));

      java.util.Iterator<RowInterval> iter = m_base_spec.getRow_intervalsIterator();
      if (iter != null && iter.hasNext()) {
        ri = iter.next();
        if (iter.hasNext()) {
          System.err.println("InputFormat only allows a single ROW interval");
          System.exit(-1);
        }
      }

      String namespace = context.getConfiguration().get(NAMESPACE);
      String tablename = context.getConfiguration().get(TABLE);

      ns = m_client.namespace_open(namespace);
      List<org.hypertable.thriftgen.TableSplit> tsplits =
          m_client.table_get_splits(ns, tablename);
      List<InputSplit> splits = new ArrayList<InputSplit>(tsplits.size());

      ByteBuffer riStartRow;
      ByteBuffer riEndRow;
      Charset charset = Charset.forName("UTF-8");
      CharsetEncoder encoder = charset.newEncoder();

      for (final org.hypertable.thriftgen.TableSplit ts : tsplits) {
        riStartRow = (ri != null && ri.isSetStart_row()) ? encoder.encode(CharBuffer.wrap(ri.getStart_row())) : null;
        riEndRow = (ri != null && ri.isSetEnd_row()) ? encoder.encode(CharBuffer.wrap(ri.getEnd_row())) : null;
        if (ri == null ||
            ((riStartRow == null || ts.end_row == null
              || ts.end_row.compareTo(riStartRow) > 0
              || (ts.end_row.compareTo(riStartRow) == 0
                  && ri.isStart_inclusive())) &&
             (riEndRow == null || ts.start_row == null
              || ts.start_row.compareTo(riEndRow) <= 0))) {
          TableSplit split = new TableSplit(tablename.getBytes("UTF-8"), ts.start_row, ts.end_row, ts.hostname);
          splits.add(split);
        }
      }
      return splits;
    }
    catch (TTransportException e) {
      e.printStackTrace();
      throw new IOException(e.getMessage());
    }
    catch (TException e) {
      e.printStackTrace();
      throw new IOException(e.getMessage());
    }
    catch (ParseException e) {
      e.printStackTrace();
      throw new IOException(e.getMessage());
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      throw new IOException(e.getMessage());
    }
    finally {
      if (ns != 0) {
        try {
          m_client.namespace_close(ns);
        }
        catch (Exception e) {
          e.printStackTrace();
          throw new IOException(e.getMessage());
        }
      }
    }

  }

}
