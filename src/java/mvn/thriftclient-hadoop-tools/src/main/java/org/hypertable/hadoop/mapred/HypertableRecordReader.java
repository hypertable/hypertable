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

import java.util.ArrayList;
import java.util.Date;
import java.util.Iterator;
import java.util.List;

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

import org.hypertable.thriftgen.*;
import org.hypertable.thrift.ThriftClient;
import org.hypertable.thrift.SerializedCellsFlag;
import org.hypertable.thrift.SerializedCellsWriter;

import org.apache.thrift.transport.TTransportException;
import org.apache.thrift.TException;

public class HypertableRecordReader
    implements org.apache.hadoop.mapred.RecordReader<Text, Text> {
  private ThriftClient m_client = null;
  private long m_ns = 0;
  private long m_scanner = 0;
  private String m_namespace = null;
  private String m_tablename = null;
  private ScanSpec m_scan_spec = null;
  private boolean m_include_timestamps = false;
  private boolean m_no_escape = false;
  private long m_bytes_read = 0;

  private List<Cell> m_cells = null;
  private Iterator<Cell> m_iter = null;
  private boolean m_eos = false;

  private Text m_key = new Text();
  private Text m_value = new Text();

  private byte[] t_row = null;
  private byte[] t_column_family = null;
  private byte[] t_column_qualifier = null;
  private byte[] t_timestamp = null;

  /* XXX make this less ugly and actually use stream.seperator */
  private byte[] tab = "\t".getBytes();
  private byte[] colon = ":".getBytes();

  /**
   *  Constructor
   *
   * @param client Hypertable Thrift client
   * @param namespace namespace containing table
   * @param tablename name of table to read from
   * @param scan_spec scan specification
   * @param include_timestamps returns timestamps
   * @param no_escape if true then do not escape \t and \n in the values
   */
  public HypertableRecordReader(ThriftClient client, String namespace,
          String tablename, ScanSpec scan_spec, boolean include_timestamps,
          boolean no_escape)
       throws IOException {
    m_client = client;
    m_namespace = namespace;
    m_tablename = tablename;
    m_scan_spec = scan_spec;
    m_include_timestamps = include_timestamps;
    m_no_escape = no_escape;
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

  public Text createKey() {
    return new Text("");
  }

  public Text createValue() {
    return new Text("");
  }

  public void close() {
    try {
      m_client.scanner_close(m_scanner);
      if (m_ns != 0)
        m_client.namespace_close(m_ns);
    }
    catch (Exception e) {
      e.printStackTrace();
    }
  }

  public long getPos() throws IOException {
    return m_bytes_read;
  }

  public float getProgress() {
    // Assume 200M split size
    if (m_bytes_read >= 200000000)
      return (float)1.0;
    return (float)m_bytes_read / (float)200000000.0;
  }

  private void fill_key(Text key, Key cell_key) {
    boolean clear = false;
    /* XXX not sure if "clear" is necessary */

    /*
     * !!
     * If the key format changes, the code which invokes fill_key()
     * will need to be adjusted because it uses a hard-coded length
     * of 24 + cell.key.row.length()!
     */

    try {
      if (m_include_timestamps && cell_key.isSetTimestamp()) {
        t_timestamp = Long.toString(cell_key.timestamp).getBytes("UTF-8");
        clear = true;
      }
      if (cell_key.isSetRow()) {
        t_row = cell_key.row.getBytes("UTF-8");
        clear = true;
      }
      if (cell_key.isSetColumn_family()) {
        t_column_family = cell_key.column_family.getBytes("UTF-8");
        clear = true;
      }
      if (cell_key.isSetColumn_qualifier()) {
        t_column_qualifier = cell_key.column_qualifier.getBytes("UTF-8");
        clear = true;
      }
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }

    if (clear) {
      key.clear();
      if (m_include_timestamps) {
        key.append(t_timestamp, 0, t_timestamp.length);
        key.append(tab, 0, tab.length);
      }
      key.append(t_row, 0, t_row.length);
      key.append(tab, 0, tab.length);
      key.append(t_column_family, 0, t_column_family.length);
      if (t_column_qualifier.length > 0) {
        key.append(colon, 0, colon.length);
        key.append(t_column_qualifier, 0, t_column_qualifier.length);
      }
    }
  }

  public boolean next(Text key, Text value) throws IOException {
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
      fill_key(key, cell.key);
      m_bytes_read += 24 + cell.key.row.length();
      if (cell.value == null || !cell.value.hasRemaining()) {
        value.set("");
      }
      else {
        // do not escape string?
        if (m_no_escape == true) {
          m_bytes_read += cell.value.remaining();
          value.set(cell.value.array(),
                  cell.value.arrayOffset() + cell.value.position(),
                  cell.value.remaining());
        }
        // escape string?
        else {
          byte[] buf = cell.value.array();
          int pos = cell.value.arrayOffset() + cell.value.position();
          int len = (int)cell.value.remaining();

          // check if we have to escape
          boolean escape = false;
          for (int i = pos; i < pos + len; i++) {
            if (buf[i] == '\n' || buf[i] == '\t'
                || buf[i] == '\0' || buf[i] == '\\') {
              escape = true;
              break;
            }
          }
          // no need to escape; copy the original value
          if (!escape) {
            m_bytes_read += cell.value.remaining();
            value.set(cell.value.array(),
                    cell.value.arrayOffset() + cell.value.position(),
                    cell.value.remaining());
          }
          // otherwise escape into a temporary ByteBuffer
          else {
            byte[] bb = new byte[len * 2];
            int j = 0;
            for (int i = pos; i < pos + len; i++) {
              if (buf[i] == '\t') {
                bb[j++] = '\\';
                bb[j++] = 't';
              }
              else if (buf[i] == '\n') {
                bb[j++] = '\\';
                bb[j++] = 'n';
              }
              else if (buf[i] == '\0') {
                bb[j++] = '\\';
                bb[j++] = '0';
              }
              else if (buf[i] == '\\') {
                bb[j++] = '\\';
                bb[j++] = '\\';
              }
              else
                bb[j++] = buf[i];
            }
            m_bytes_read += j;
            value.set(bb, 0, j);
          }
        }
      }

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

