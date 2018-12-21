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

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CharsetEncoder;

import org.hypertable.hadoop.util.Serialization;

import org.hypertable.thriftgen.*;

import org.hypertable.hadoop.mapreduce.ScanSpec;

import org.apache.hadoop.io.Writable;
import org.apache.hadoop.mapred.InputSplit;

/**
 * A table split corresponds to a key range (low, high). All references to row
 * below refer to the key of the row.
 */
public class TableSplit
implements InputSplit,Comparable<TableSplit> {

  private byte [] m_tablename = null;
  private ByteBuffer m_startrow = null;
  private ByteBuffer m_endrow = null;
  private String m_hostname = null;
  private ScanSpec m_scanspec = null;

  /**
   * Constructs a new instance while assigning all variables.
   *
   * @param tableName  The name of the current table.
   * @param startRow  The start row of the split.
   * @param endRow  The end row of the split.
   * @param hostname  The hostname of the range.
   */
  public TableSplit(byte [] tableName, ByteBuffer startRow, ByteBuffer endRow,
                    final String hostname) {
    this.m_tablename = tableName;
    this.m_startrow = startRow;
    this.m_endrow = endRow;
    this.m_hostname = hostname;
    // Check for END_ROW marker
    if (endRow != null && endRow.limit() == 2 &&
        endRow.get(0) == (byte)0xff && endRow.get(1) == (byte)0xff) {
      this.m_endrow = null;
    }
  }

  /** Empty constructor */
  public TableSplit() { }

  /**
   * Returns the table name.
   *
   * @return The table name.
   */
  public byte [] getTableName() {
    return m_tablename;
  }

  /**
   * Returns the start row.
   *
   * @return The start row.
   */
  public ByteBuffer getStartRow() {
    return m_startrow;
  }

  /**
   * Returns the end row.
   *
   * @return The end row.
   */
  public ByteBuffer getEndRow() {
    return m_endrow;
  }

  /**
   * Returns the range location.
   *
   * @return The range's location.
   */
  public String getRangeLocation() {
    return m_hostname;
  }

  /**
   * Returns the range's location as an array.
   *
   * @return The array containing the range location.
   * @see org.apache.hadoop.mapreduce.InputSplit#getLocations()
   */
  public String[] getLocations() {
    return new String[] {m_hostname};
  }

  /**
   * Returns the length of the split.
   *
   * @return The length of the split.
   * @see org.apache.hadoop.mapreduce.InputSplit#getLength()
   */
  public long getLength() {
    // Not clear how to obtain this... seems to be used only for sorting splits
    return 0;
  }


  /**
   * Updates the ScanSpec by setting the row interval to match this split
   *
   * @param scan_spec The base ScanSpec to start with
   * @return a new scan_spec object with a row interval matching this split
   */
  public ScanSpec createScanSpec(ScanSpec base_spec) {
    ScanSpec scan_spec = new ScanSpec(base_spec);

    RowInterval interval = new RowInterval();

    scan_spec.unsetRow_intervals();

    try {

      if(m_startrow != null && m_startrow.limit() > 0) {
        interval.setStart_row_binary(m_startrow);
        interval.setStart_inclusive(false);
        interval.setStart_inclusiveIsSet(true);
      }

      if(m_endrow != null && m_endrow.limit() > 0) {
        interval.setEnd_row_binary(m_endrow);
        interval.setEnd_inclusive(true);
        interval.setEnd_inclusiveIsSet(true);
      }

      ByteBuffer riStartRow;
      ByteBuffer riEndRow;
      Charset charset = Charset.forName("UTF-8");
      CharsetEncoder encoder = charset.newEncoder();

      if (base_spec.isSetRow_intervals()) {
        for (RowInterval ri : base_spec.getRow_intervals()) {
          riStartRow = (ri != null && ri.isSetStart_row()) ? encoder.encode(CharBuffer.wrap(ri.getStart_row())) : null;
          riEndRow = (ri != null && ri.isSetEnd_row()) ? encoder.encode(CharBuffer.wrap(ri.getEnd_row())) : null;
          if (riStartRow != null) {
            if (m_startrow == null || m_startrow.limit() == 0 ||
                riStartRow.compareTo(m_startrow) > 0) {
              interval.setStart_row_binary(riStartRow);
              interval.setStart_inclusive( ri.isStart_inclusive() );
              interval.setStart_inclusiveIsSet(true);
            }
          }
          if (riEndRow != null) {
            if (m_endrow == null || m_endrow.limit() == 0 ||
                riEndRow.compareTo(m_endrow) < 0) {
              interval.setEnd_row_binary(riEndRow);
              interval.setEnd_inclusive( ri.isEnd_inclusive() );
              interval.setEnd_inclusiveIsSet(true);
            }
          }
          // Only allowing a single row interval
          break;
        }
      }

    }
    catch (CharacterCodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }

    if(interval.isSetStart_row_binary() || interval.isSetEnd_row_binary()) {
      scan_spec.addToRow_intervals(interval);
      scan_spec.setRow_intervalsIsSet(true);
    }

    return scan_spec;
  }

  /**
   * Reads the values of each field.
   *
   * @param in  The input to read from.
   * @throws IOException When reading the input fails.
   */
  public void readFields(DataInput in) throws IOException {
    m_tablename = Serialization.readByteArray(in);
    m_startrow = ByteBuffer.wrap(Serialization.readByteArray(in));
    m_endrow = ByteBuffer.wrap(Serialization.readByteArray(in));
    m_hostname = Serialization.toString(Serialization.readByteArray(in));
  }

  /**
   * Writes the field values to the output.
   *
   * @param out  The output to write to.
   * @throws IOException When writing the values to the output fails.
   */
  public void write(DataOutput out) throws IOException {
    Serialization.writeByteArray(out, m_tablename);
    Serialization.writeByteBuffer(out, m_startrow);
    Serialization.writeByteBuffer(out, m_endrow);
    Serialization.writeByteArray(out, Serialization.toBytes(m_hostname));
  }

  /**
   * Returns the details about this instance as a string.
   *
   * @return The values of this instance as a string.
   * @see java.lang.Object#toString()
   */
  public String toString() {
    String start_str = new String();
    String end_str = new String();

    if (m_startrow != null)
      start_str = Serialization.toStringBinary(m_startrow);

    if (m_endrow != null)
      end_str = Serialization.toStringBinary(m_endrow);

    return m_hostname + ":" + start_str + "," + end_str;
  }

  public int compareTo(TableSplit split) {
    return m_startrow.compareTo(split.getStartRow());
  }

  /* XXX add equals for columns */
  public boolean equal(TableSplit split) {
    return Serialization.equals(m_tablename, split.m_tablename) &&
      m_startrow.compareTo(split.getStartRow()) == 0 &&
      m_endrow.compareTo(split.getEndRow()) == 0 &&
      m_hostname.equals(split.m_hostname);
  }

}
