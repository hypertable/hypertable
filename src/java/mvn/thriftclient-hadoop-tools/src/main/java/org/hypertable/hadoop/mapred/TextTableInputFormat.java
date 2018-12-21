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
import java.nio.CharBuffer;
import java.nio.charset.Charset;
import java.nio.charset.CharsetEncoder;

import java.text.ParseException;
import java.util.ArrayList;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.Vector;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapred.InputFormat;
import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.mapred.InputSplit;
import org.apache.hadoop.mapred.RecordReader;
import org.apache.hadoop.mapred.Reporter;
import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.mapred.JobConfigurable;

import org.hypertable.thriftgen.*;
import org.hypertable.Common.HostSpecification;
import org.hypertable.Common.Time;
import org.hypertable.hadoop.mapreduce.ScanSpec;
import org.hypertable.hadoop.mapred.TableSplit;

import org.hypertable.thrift.ThriftClient;

import org.apache.thrift.transport.TTransportException;
import org.apache.thrift.TException;

public class TextTableInputFormat
implements org.apache.hadoop.mapred.InputFormat<Text, Text>, JobConfigurable {

  final Log LOG = LogFactory.getLog(InputFormat.class);

  public static final String NAMESPACE          = "hypertable.mapreduce.namespace";
  public static final String INPUT_NAMESPACE    = "hypertable.mapreduce.input.namespace";
  public static final String TABLE              = "hypertable.mapreduce.input.table";
  public static final String COLUMNS            = "hypertable.mapreduce.input.scan_spec.columns";
  public static final String VALUE_REGEXPS      = "hypertable.mapreduce.input.scan_spec.value_regexps";
  public static final String COLUMN_PREDICATES  = "hypertable.mapreduce.input.scan_spec.column_predicates";
  public static final String OPTIONS            = "hypertable.mapreduce.input.scan_spec.options";
  public static final String ROW_INTERVAL       = "hypertable.mapreduce.input.scan_spec.row_interval";
  public static final String TIMESTAMP_INTERVAL = "hypertable.mapreduce.input.scan_spec.timestamp_interval";
  public static final String INCLUDE_TIMESTAMPS = "hypertable.mapreduce.input.include_timestamps";
  public static final String NO_ESCAPE          = "hypertable.mapreduce.input.no_escape";
  public static final String THRIFT_FRAMESIZE   = "hypertable.mapreduce.thriftbroker.framesize";
  public static final String THRIFT_FRAMESIZE2  = "hypertable.mapreduce.thriftclient.framesize";
  public static final String THRIFT_HOST        = "hypertable.mapreduce.thriftbroker.host";
  public static final String THRIFT_PORT        = "hypertable.mapreduce.thriftbroker.port";

  private ThriftClient m_client = null;
  private ScanSpec m_base_spec = null;
  private String m_namespace = null;
  private String m_tablename = null;
  private boolean m_include_timestamps = false;
  private boolean m_no_escape = false;

  private static String stripQuotes(String str) {
    if (str != null && str.length() > 0) {
      if ((str.charAt(0) == '\'' && str.charAt(str.length()-1) == '\'') ||
          (str.charAt(0) == '"' && str.charAt(str.length()-1) == '"'))
        return str.substring(1, str.length()-1);
    }
    return str;
  }

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
  private String getThriftHost(JobConf job) throws ParseException {
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
   * Gets the ThriftBroker framesize.
   * Obtains the framesize by first reading the 
   * "hypertable.mapreduce.thriftbroker.framesize" property and if that is zero,
   * reads the deprecated "hypertable.mapreduce.thriftclient.framesize"
   * @param job Job configuration
   * @return ThriftBroker frame size
   */
  private int getThriftFramesize(JobConf job) {
    int framesize = job.getInt(THRIFT_FRAMESIZE, 0);
    if (framesize == 0)
      framesize = job.getInt(THRIFT_FRAMESIZE2, 0);
    return framesize;
  }

  public void parseOptions(JobConf job) throws ParseException {
    String str = job.get(OPTIONS);
    if (str != null) {
      String [] strs = str.split("\\s");
      for (int i = 0; i < strs.length; i++) {
        strs[i] = strs[i].toUpperCase();
        if (strs[i].equals("MAX_VERSIONS") || strs[i].equals("REVS")) {
          i++;
          if (i == strs.length)
            throw new ParseException("Bad OPTIONS spec", i);
          int value = Integer.parseInt(strs[i]);
          m_base_spec.setVersions(value);
        }
        else if (strs[i].equals("OFFSET")) {
          i++;
          if (i == strs.length)
            throw new ParseException("Bad OPTIONS spec", i);
          int value = Integer.parseInt(strs[i]);
          m_base_spec.setRow_offset(value);
        }
        else if (strs[i].equals("CELL_OFFSET")) {
          i++;
          if (i == strs.length)
            throw new ParseException("Bad OPTIONS spec", i);
          int value = Integer.parseInt(strs[i]);
          m_base_spec.setCell_offset(value);
        }
        else if (strs[i].equals("LIMIT")) {
          i++;
          if (i == strs.length)
            throw new ParseException("Bad OPTIONS spec", i);
          int value = Integer.parseInt(strs[i]);
          m_base_spec.setRow_limit(value);
        }
        else if (strs[i].equals("CELL_LIMIT")) {
          i++;
          if (i == strs.length)
            throw new ParseException("Bad OPTIONS spec", i);
          int value = Integer.parseInt(strs[i]);
          m_base_spec.setCell_limit(value);
        }
        else if (strs[i].equals("CELL_LIMIT_PER_FAMILY")) {
          i++;
          if (i == strs.length)
            throw new ParseException("Bad OPTIONS spec", i);
          int value = Integer.parseInt(strs[i]);
          m_base_spec.setCell_limit_per_family(value);
        }
        else if (strs[i].equals("KEYS_ONLY")) {
          m_base_spec.setKeys_only(true);
        }
        else if (strs[i].equals("RETURN_DELETES")) {
          m_base_spec.setReturn_deletes(true);
        }
        else
          throw new ParseException("Bad OPTIONS spec: "+strs[i], i);
      }
    }
  }

  public void parseColumns(JobConf job) {
    String str = job.get(COLUMNS);
    if (str != null) {
      String [] columns = str.split(",");
      for (int i = 0; i < columns.length; i++)
        m_base_spec.addToColumns(stripQuotes(columns[i]));
    }
  }

  public void parseValueRegexps(JobConf job) {
    String str = job.get(VALUE_REGEXPS);
    if (str != null)
      m_base_spec.setValue_regexp(stripQuotes(str));
  }

  public void parseColumnPredicate(JobConf job) throws ParseException {
    ColumnPredicate cp = new ColumnPredicate();
    String str = job.get(COLUMN_PREDICATES);
    if (str == null)
      return;
    int offset = str.indexOf("=^");
    if (offset != -1) {
      cp.column_family = str.substring(0, offset).trim();
      cp.operation = ColumnPredicateOperation.PREFIX_MATCH.getValue();
      cp.value = str.substring(offset + 2).trim();
    }
    else {
      offset = str.indexOf("=");
      if (offset != -1) {
        cp.column_family = str.substring(0, offset).trim();
        cp.operation = ColumnPredicateOperation.EXACT_MATCH.getValue();
        cp.value = str.substring(offset + 1).trim();
      }
      else
        throw new ParseException("Invalid COLUMN_PREDICATE: "+str, 0);
    }
    m_base_spec.addToColumn_predicates(cp);
  }

  public String [] parseRelopSpec(String str, String name) {
    String name_uppercase = name.toUpperCase();
    String name_lowercase = name.toLowerCase();
    String [] strs = new String [5];
    int ts_offset = str.indexOf(name_uppercase);
    if (ts_offset == -1)
      ts_offset = str.indexOf(name_lowercase);
    if (ts_offset == -1)
      return null;

    strs[0] = str.substring(0, ts_offset).trim();
    strs[2] = str.substring(ts_offset, ts_offset+name.length());
    strs[4] = str.substring(ts_offset + name.length()).trim();

    if (strs[0].length() > 0) {
      int offset = strs[0].length()-1;
      while (offset > 0 &&
             (strs[0].charAt(offset) == '<' ||
              strs[0].charAt(offset) == '=' ||
              strs[0].charAt(offset) == '>'))
        offset--;
      if (offset == -1 || offset == strs[0].length()-1)
        return null;
      strs[1] = strs[0].substring(offset+1);
      strs[0] = strs[0].substring(0, offset).trim();
    }

    if (strs[4].length() > 0) {
      int offset = 0;
      while (offset < strs[4].length() &&
             (strs[4].charAt(offset) == '<' ||
              strs[4].charAt(offset) == '=' ||
              strs[4].charAt(offset) == '>'))
        offset++;
      if (offset == strs[4].length() || offset == 0)
        return null;
      strs[3] = strs[4].substring(0, offset);
      strs[4] = strs[4].substring(offset).trim();
    }

    if (strs[0].length() == 0 && strs[4].length() == 0)
      return null;

    if (strs[0].length() == 0) {
      if (strs[3].equals(">") || strs[3].equals(">=")) {
        strs[0] = strs[4];
        if (strs[3].equals(">"))
          strs[1] = "<=";
        else if (strs[3].equals(">="))
          strs[1] = "<";
        strs[3] = null;
        strs[4] = null;
      }
    }
    else if (strs[4].length() == 0) {
      if (strs[1].equals(">") || strs[1].equals(">=")) {
        strs[4] = strs[0];
        if (strs[1].equals(">"))
          strs[3] = "<=";
        else if (strs[1].equals(">="))
          strs[3] = "<";
        strs[1] = null;
        strs[0] = null;
      }
    }
    else {
      if (strs[1].equals(">") || strs[1].equals(">=")) {
        if (!strs[3].equals(">") && !strs[3].equals(">="))
          return null;
        String tmp = strs[0];
        strs[0] = strs[4];
        strs[4] = tmp;
        if (strs[1].equals(">"))
          strs[1] = "<=";
        else if (strs[1].equals(">="))
          strs[1] = "<";
        if (strs[3].equals(">"))
          strs[3] = "<=";
        else if (strs[3].equals(">="))
          strs[3] = "<";
      }
    }

    if (strs[1] != null && strs[1].equals("=")
            && strs[3] != null && strs[3].equals("="))
      return null;

    strs[0] = stripQuotes(strs[0]);
    strs[4] = stripQuotes(strs[4]);

    return strs;
  }

  public void parseTimestampInterval(JobConf job) throws ParseException {
    String str = job.get(TIMESTAMP_INTERVAL);
    if (str != null) {
      Date ts;
      long epoch_time;
      String [] parsedRelop = parseRelopSpec(str, "TIMESTAMP");

      if (parsedRelop == null)
        throw new ParseException("Invalid TIMESTAMP interval: "+str, 0);

      if (parsedRelop[0] != null && parsedRelop[0].length() > 0) {
        ts = Time.parse_ts(parsedRelop[0]);
        epoch_time = ts.getTime() * 1000000;
        m_base_spec.setStart_time(epoch_time);
        if (parsedRelop[1].equals("="))
          m_base_spec.setEnd_time(epoch_time);
      }

      if (parsedRelop[4] != null && parsedRelop[4].length() > 0) {
        ts = Time.parse_ts(parsedRelop[4]);
        epoch_time = ts.getTime() * 1000000;
        m_base_spec.setEnd_time(epoch_time);
        if (parsedRelop[3].equals("="))
          m_base_spec.setStart_time(epoch_time);
      }
    }
  }

  public void parseRowInterval(JobConf job) throws ParseException {
    String str = job.get(ROW_INTERVAL);
    if (str != null) {
      Date ts;
      long epoch_time;
      String [] parsedRelop = parseRelopSpec(str, "ROW");
      RowInterval interval = new RowInterval();

      if (parsedRelop == null)
        throw new ParseException("Invalid ROW interval: "+str, 0);

      if (parsedRelop[0] != null && parsedRelop[0].length() > 0) {
        interval.setStart_row(parsedRelop[0]);
        interval.setStart_rowIsSet(true);
        if (parsedRelop[1].equals("<"))
          interval.setStart_inclusive(false);
        else if (parsedRelop[1].equals("<="))
          interval.setStart_inclusive(true);
        else
          throw new ParseException("Invalid ROW interval, bad RELOP ("
                  + parsedRelop[1] + ")", 0);        
        interval.setStart_inclusiveIsSet(true);
      }

      if (parsedRelop[4] != null && parsedRelop[4].length() > 0) {
        interval.setEnd_row(parsedRelop[4]);
        interval.setEnd_rowIsSet(true);
        if (parsedRelop[3].equals("<"))
          interval.setEnd_inclusive(false);
        else if (parsedRelop[3].equals("<="))
          interval.setEnd_inclusive(true);
        else
          throw new ParseException("Invalid ROW interval, bad RELOP ("
                  + parsedRelop[3] + ")", 0);
        interval.setEnd_inclusiveIsSet(true);
      }

      if (interval.isSetStart_row() || interval.isSetEnd_row()) {
        m_base_spec.addToRow_intervals(interval);
        m_base_spec.setRow_intervalsIsSet(true);
      }
    }
  }

  public void configure(JobConf job)
  {
    m_include_timestamps = job.getBoolean(INCLUDE_TIMESTAMPS, false);
    m_no_escape = job.getBoolean(NO_ESCAPE, false);
    try {
      m_base_spec = new ScanSpec();

      parseColumns(job);
      parseOptions(job);
      parseTimestampInterval(job);
      parseRowInterval(job);
      parseValueRegexps(job);
      parseColumnPredicate(job);

      System.err.println(m_base_spec);
    }
    catch (Exception e) {
      e.printStackTrace();
      System.exit(-1);
    }

  }

  public RecordReader<Text, Text> getRecordReader(
      InputSplit split, JobConf job, Reporter reporter)
  throws IOException {
    try {
      TableSplit ts = (TableSplit)split;
      if (m_namespace == null) {
        m_namespace = job.get(INPUT_NAMESPACE);
        if (m_namespace == null)
          m_namespace = job.get(NAMESPACE);
      }
      if (m_tablename == null)
        m_tablename = job.get(TABLE);
      ScanSpec scan_spec = ts.createScanSpec(m_base_spec);
      System.err.println(scan_spec);

      if (m_client == null) {
        String host = getThriftHost(job);
        int port = job.getInt(THRIFT_PORT, 15867);
        int framesize = getThriftFramesize(job);
        if (framesize != 0)
          m_client = ThriftClient.create(host, port, 1600000, true, framesize);
        else
          m_client = ThriftClient.create(host, port);
      }
      return new HypertableRecordReader(m_client, m_namespace, m_tablename,
              scan_spec, m_include_timestamps, m_no_escape);
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

  public InputSplit[] getSplits(JobConf job, int numSplits) throws IOException {
    long ns = 0;

    try {
      RowInterval ri = null;

      if (m_client == null) {
        String host = getThriftHost(job);
        int port = job.getInt(THRIFT_PORT, 15867);
        int framesize = getThriftFramesize(job);
        if (framesize != 0)
          m_client = ThriftClient.create(host, port, 1600000, true, framesize);
        else
          m_client = ThriftClient.create(host, port);
      }

      String tablename = job.get(TABLE);
      String namespace = job.get(INPUT_NAMESPACE);
      if (namespace == null)
        namespace = job.get(NAMESPACE);

      java.util.Iterator<RowInterval> iter = m_base_spec.getRow_intervalsIterator();
      if (iter != null && iter.hasNext()) {
        ri = iter.next();
        if (iter.hasNext()) {
          System.err.println("InputFormat only allows a single ROW interval");
          System.exit(-1);
        }
      }

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

      InputSplit[] isplits = new InputSplit[splits.size()];
      return splits.toArray(isplits);
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
