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
import java.io.DataInput;
import java.io.DataOutput;
import java.io.UnsupportedEncodingException;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import org.apache.hadoop.io.WritableComparable;
import org.apache.hadoop.io.WritableUtils;

import org.hypertable.Common.Checksum;
import org.hypertable.Common.CString;

import org.hypertable.thriftgen.*;
import org.hypertable.thrift.SerializedCellsFlag;
import org.hypertable.thrift.ThriftClient;

/**
 * Write Map/Reduce output to a table in Hypertable.
 *
 * TODO: For now we assume ThriftBroker is running on localhost on default port (15867).
 * Change this to read from configs at some point.
 */
public class KeyWritable extends Key implements WritableComparable<Key> {

  public KeyWritable() { super(); }

  /**
   * Sets the row
   *
   * @param row row string
   * @return this key object
   */
  public Key setRow(String row) {
    this.row = row;
    this.row_buffer = null;
    this.row_buffer_offset = 0;
    this.row_buffer_length = 0;
    m_dirty = true;
    return this;
  }

  /**
   * Sets the row key with byte sequence
   *
   * @param buffer byte array holding row bytes
   * @param offset offset within byte array holding row key
   * @param length of row key
   * @return this key object
   */
  public Key setRow(byte [] buffer, int offset, int length) {
    this.row = null;
    this.row_buffer = buffer;
    this.row_buffer_offset = offset;
    this.row_buffer_length = length;
    m_dirty = true;
    return this;
  }

  /**
   * Returns row key string
   *
   * @return row key string
   */
  public String getRow() {
    if (this.row == null) {
      if (row_buffer != null && row_buffer_length > 0) {
        try {
          row = new String(row_buffer, row_buffer_offset, row_buffer_length, "UTF-8");
        }
        catch (UnsupportedEncodingException e) {
          e.printStackTrace();
          System.exit(-1);
        }
      }
    }
    return row;
  }

  /** Returns byte array holding the row.
   * @return byte array holding row
   */
  public byte [] getRowBytes() {
    return row_buffer;
  }

  /** Returns offset within byte array of beginning of row.
   * @return offset within byte array of beginning of row
   */
  public int getRowOffset() {
    return row_buffer_offset;
  }

  /** Returns row length
   * @return row length
   */
  public int getRowLength() {
    return row_buffer_length;
  }

  /**
   * Sets the column family
   *
   * @param column_family column family string
   * @return this key object
   */
  public Key setColumn_family(String column_family) {
    this.column_family = column_family;
    this.column_family_buffer = null;
    this.column_family_buffer_offset = 0;
    this.column_family_buffer_length = 0;
    m_dirty = true;
    return this;
  }

  /**
   * Sets the column family key with byte sequence
   *
   * @param buffer byte array holding column family bytes
   * @param offset offset within byte array holding column family string
   * @param length of column family string
   * @return this key object
   */
  public Key setColumn_family(byte [] buffer, int offset, int length) {
    this.column_family = null;
    this.column_family_buffer = buffer;
    this.column_family_buffer_offset = offset;
    this.column_family_buffer_length = length;
    m_dirty = true;
    return this;
  }

  /**
   * Returns column family string
   *
   * @return column family string
   */
  public String getColumn_family() {
    if (this.column_family == null) {
      if (column_family_buffer != null && column_family_buffer_length > 0) {
        try {
          column_family = new String(column_family_buffer,
                                     column_family_buffer_offset,
                                     column_family_buffer_length, "UTF-8");
        }
        catch (UnsupportedEncodingException e) {
          e.printStackTrace();
          System.exit(-1);
        }
      }
    }
    return column_family;
  }

  /** Returns byte array holding the column family.
   * @return byte array holding column family
   */
  public byte [] getColumnFamilyBytes() {
    return column_family_buffer;
  }

  /** Returns offset within byte array of beginning of column family.
   * @return offset within byte array of beginning of column family
   */
  public int getColumnFamilyOffset() {
    return column_family_buffer_offset;
  }

  /** Returns column family length
   * @return column family length
   */
  public int getColumnFamilyLength() {
    return column_family_buffer_length;
  }

  /**
   * Sets the column qualifier
   *
   * @param column_qualifier column qualifier string
   * @return this key object
   */
  public Key setColumn_qualifier(String column_qualifier) {
    this.column_qualifier = column_qualifier;
    this.column_qualifier_buffer = null;
    this.column_qualifier_buffer_offset = 0;
    this.column_qualifier_buffer_length = 0;
    m_dirty = true;
    return this;
  }

  /**
   * Sets the column qualifier key with byte sequence
   *
   * @param buffer byte array holding column qualifier bytes
   * @param offset offset within byte array holding column qualifier string
   * @param length of column qualifier string
   * @return this key object
   */
  public Key setColumn_qualifier(byte [] buffer, int offset, int length) {
    this.column_qualifier = null;
    this.column_qualifier_buffer = buffer;
    this.column_qualifier_buffer_offset = offset;
    this.column_qualifier_buffer_length = length;
    m_dirty = true;
    return this;
  }

  /**
   * Returns column qualifier string
   *
   * @return column qualifier string
   */
  public String getColumn_qualifier() {
    if (this.column_qualifier == null) {
      if (column_qualifier_buffer != null && column_qualifier_buffer_length > 0) {
        try {
          column_qualifier = new String(column_qualifier_buffer,
                                        column_qualifier_buffer_offset,
                                        column_qualifier_buffer_length, "UTF-8");
        }
        catch (UnsupportedEncodingException e) {
          e.printStackTrace();
          System.exit(-1);
        }
      }
    }
    return column_qualifier;
  }

  /** Returns byte array holding the column qualifier.
   * @return byte array holding column qualifier
   */
  public byte [] getColumnQualifierBytes() {
    return column_qualifier_buffer;
  }

  /** Returns offset within byte array of beginning of column qualifier.
   * @return offset within byte array of beginning of column qualifier
   */
  public int getColumnQualifierOffset() {
    return column_qualifier_buffer_offset;
  }

  /** Returns column qualifier length
   * @return column qualifier length
   */
  public int getColumnQualifierLength() {
    return column_qualifier_buffer_length;
  }

  public Key setTimestamp(long timestamp) {
    this.timestamp = timestamp;
    setTimestampIsSet(true);
    m_dirty = true;
    return this;
  }

  public Key setRevision(long revision) {
    this.revision = revision;
    setRevisionIsSet(true);
    m_dirty = true;
    return this;
  }

  @Override
  public void readFields(DataInput in) throws IOException {
    int len = WritableUtils.readVInt(in);

    if (m_input_buffer == null || len > m_input_buffer.capacity()) {
      m_input_buffer = ByteBuffer.allocate(len+16);
      m_input_buffer.order(ByteOrder.LITTLE_ENDIAN);
    }
    else {
      m_input_buffer.clear();
      m_input_buffer.limit(len);
    }

    in.readFully(m_input_buffer.array(), 0, len);

    short flagVal = m_input_buffer.getShort();
    flag = org.hypertable.thriftgen.KeyFlag.findByValue((int)flagVal);
    if (flag == null)
      throw new IOException("Can't convert KeyFlag value " + flagVal + " to enum");

    len = readVInt(m_input_buffer);
    if (len > 0) {
      row_buffer = m_input_buffer.array();
      row_buffer_offset = m_input_buffer.position();
      m_input_buffer.position(row_buffer_offset+len);
    }
    else {
      row_buffer = null;
      row_buffer_offset = 0;
    }
    row_buffer_length = len;
    row = null;

    len = readVInt(m_input_buffer);
    if (len > 0) {
      column_family_buffer = m_input_buffer.array();
      column_family_buffer_offset = m_input_buffer.position();
      m_input_buffer.position(column_family_buffer_offset+len);
    }
    else {
      column_family_buffer = null;
      column_family_buffer_offset = 0;
    }
    column_family_buffer_length = len;
    column_family = null;

    len = readVInt(m_input_buffer);
    if (len > 0) {
      column_qualifier_buffer = m_input_buffer.array();
      column_qualifier_buffer_offset = m_input_buffer.position();
      m_input_buffer.position(column_qualifier_buffer_offset+len);
    }
    else {
      column_qualifier_buffer = null;
      column_qualifier_buffer_offset = 0;
    }
    column_qualifier_buffer_length = len;
    column_qualifier = null;
    
    timestamp = m_input_buffer.getLong();
    setTimestampIsSet(true);

    revision = m_input_buffer.getLong();
    setRevisionIsSet(true);

    m_dirty = false;
  }

  @Override
  public void write(DataOutput out) throws IOException {

    if (!m_dirty) {
      WritableUtils.writeVInt(out, m_input_buffer.position());
      out.write(m_input_buffer.array(), 0, m_input_buffer.position());
      return;
    }

    convert_strings_to_buffers();

    int position;
    int serial_length = row_buffer_length + column_family_buffer_length + 
      column_qualifier_buffer_length + 30;

    if (m_output_buffer == null || serial_length > m_output_buffer.capacity()) {
      m_output_buffer = ByteBuffer.allocate(serial_length+16);
      m_output_buffer.order(ByteOrder.LITTLE_ENDIAN);
    }
    else {
      m_output_buffer.clear();
      m_output_buffer.limit(serial_length);
    }

    m_output_buffer.putShort((short)flag.getValue());

    if (row_buffer != null) {
      writeVInt(m_output_buffer, row_buffer_length);
      position = m_output_buffer.position();
      m_output_buffer.put(row_buffer, row_buffer_offset, row_buffer_length);
      row_buffer = m_output_buffer.array();
      row_buffer_offset = position;
    }
    else
      writeVInt(m_output_buffer, 0);

    if (column_family_buffer != null) {
      writeVInt(m_output_buffer, column_family_buffer_length);
      position = m_output_buffer.position();
      m_output_buffer.put(column_family_buffer, column_family_buffer_offset, column_family_buffer_length);
      column_family_buffer = m_output_buffer.array();
      column_family_buffer_offset = position;
    }
    else
      writeVInt(m_output_buffer, 0);

    if (column_qualifier_buffer != null) {
      writeVInt(m_output_buffer, column_qualifier_buffer_length);
      position = m_output_buffer.position();
      m_output_buffer.put(column_qualifier_buffer, column_qualifier_buffer_offset, column_qualifier_buffer_length);
      column_qualifier_buffer = m_output_buffer.array();
      column_qualifier_buffer_offset = position;
    }
    else
      writeVInt(m_output_buffer, 0);

    if (!isSetTimestamp()) {
      timestamp = SerializedCellsFlag.AUTO_ASSIGN;
      setTimestampIsSet(true);
    }
    m_output_buffer.putLong(timestamp);

    if (!isSetRevision()) {
      revision = SerializedCellsFlag.AUTO_ASSIGN;
      setRevisionIsSet(true);
    }
    m_output_buffer.putLong(revision);

    WritableUtils.writeVInt(out, m_output_buffer.position());
    out.write(m_output_buffer.array(), 0, m_output_buffer.position());
  }

  @Override
    public int hashCode() {

    try {
      if (row_buffer == null)
        row_string_to_buffer();
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }

    if (row_buffer != null)
      return Checksum.fletcher32(row_buffer, row_buffer_offset, row_buffer_length);
    
    return 0;
  }


  /** TBD
  public int compare(byte[] b1, int s1, int l1,
                     byte[] b2, int s2, int l2) {
  }
  */

  public int compareTo(Key other_key) {
    int cmp;
    KeyWritable other;

    try {
      other = (KeyWritable)other_key;
    }
    catch (ClassCastException e) {
      convert_buffers_to_strings();
      return super.compareTo(other_key);
    }

    convert_strings_to_buffers();
    other.convert_strings_to_buffers();

    cmp = CString.memcmp(row_buffer, row_buffer_offset, row_buffer_length,
                         other.row_buffer, other.row_buffer_offset,
                         other.row_buffer_length);
    if (cmp != 0)
      return cmp;

    cmp = CString.memcmp(column_family_buffer, column_family_buffer_offset,
                         column_family_buffer_length, other.column_family_buffer,
                         other.column_family_buffer_offset,
                         other.column_family_buffer_length);
    if (cmp != 0)
      return cmp;

    cmp = CString.memcmp(column_qualifier_buffer, column_qualifier_buffer_offset,
                         column_qualifier_buffer_length,
                         other.column_qualifier_buffer,
                         other.column_qualifier_buffer_offset,
                         other.column_qualifier_buffer_length);

    return cmp;
  }



  public void load(Key key) { 
    try {
      if (key.isSetRow()) {
        row = key.row;
        setRowIsSet(true);
        row_string_to_buffer();
      }
      if (key.isSetColumn_family()) {
        column_family = key.column_family;
        setColumn_familyIsSet(true);
        column_family_string_to_buffer();
      }
      if (key.isSetColumn_qualifier()) {
        column_qualifier = key.column_qualifier;
        setColumn_qualifierIsSet(true);
        column_qualifier_string_to_buffer();
      }
      if (key.isSetFlag()) {
        flag = key.flag;
        setFlagIsSet(true);
      }
      if (key.isSetTimestamp()) {
        timestamp = key.timestamp;
        setTimestampIsSet(true);
      }
      else
        timestamp = SerializedCellsFlag.AUTO_ASSIGN;
      if (key.isSetRevision()) {
        revision = key.revision;
        setRevisionIsSet(true);
      }
      else
        revision = SerializedCellsFlag.AUTO_ASSIGN;
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }

  }

  public void convert_strings_to_buffers() {

    try {
      if (row_buffer == null)
        row_string_to_buffer();

      if (column_family_buffer == null)
        column_family_string_to_buffer();

      if (column_qualifier_buffer == null)
        column_qualifier_string_to_buffer();
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }
  }

  private void row_string_to_buffer() throws UnsupportedEncodingException {
    if (row != null) {
      row_buffer = row.getBytes("UTF-8");
      row_buffer_offset = 0;
      row_buffer_length = row_buffer.length;
      m_dirty = true;
    }
  }

  private void column_family_string_to_buffer() throws UnsupportedEncodingException {
    if (column_family != null) {
      column_family_buffer = column_family.getBytes("UTF-8");
      column_family_buffer_offset = 0;
      column_family_buffer_length = column_family_buffer.length;
      m_dirty = true;
    }
  }

  private void column_qualifier_string_to_buffer() throws UnsupportedEncodingException {
    if (column_qualifier != null) {
      column_qualifier_buffer = column_qualifier.getBytes("UTF-8");
      column_qualifier_buffer_offset = 0;
      column_qualifier_buffer_length = column_qualifier_buffer.length;
      m_dirty = true;
    }
  }

  public void convert_buffers_to_strings() {

    try {
      if (row == null)
        row_buffer_to_string();

      if (column_family == null)
        column_family_buffer_to_string();

      if (column_qualifier == null)
        column_qualifier_buffer_to_string();
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }
  }

  private void row_buffer_to_string() throws UnsupportedEncodingException {
    if (row_buffer != null)
      row = new String(row_buffer, row_buffer_offset, row_buffer_length, "UTF-8");
    else
      row = new String();
  }

  private void column_family_buffer_to_string() throws UnsupportedEncodingException {
    if (column_family_buffer != null)
      column_family = new String(column_family_buffer,
                                 column_family_buffer_offset,
                                 column_family_buffer_length, "UTF-8");
    else
      column_family = new String();
  }

  private void column_qualifier_buffer_to_string() throws UnsupportedEncodingException {
    if (column_qualifier_buffer != null)
      column_qualifier = new String(column_qualifier_buffer,
                                    column_qualifier_buffer_offset,
                                    column_qualifier_buffer_length, "UTF-8");
    else
      column_qualifier = new String();
  }

  /**
   * Serializes a long to a byte buffer with zero-compressed encoding.
   * For -112 <= i <= 127, only one byte is used with the actual value.
   * For other values of i, the first byte value indicates whether the
   * long is positive or negative, and the number of bytes that follow.
   * If the first byte value v is between -113 and -120, the following long
   * is positive, with number of bytes that follow are -(v+112).
   * If the first byte value v is between -121 and -128, the following long
   * is negative, with number of bytes that follow are -(v+120). Bytes are
   * stored in the high-non-zero-byte-first order.
   * 
   * @param buf Byte buffer to serialize to
   * @param i Integer to be serialized
   * @throws java.io.IOException 
   */
  public static void writeVInt(ByteBuffer buf, int i) throws IOException {
    if (i >= -112 && i <= 127) {
      buf.put((byte)i);
      return;
    }
      
    int len = -112;
    if (i < 0) {
      i ^= -1L; // take one's complement'
      len = -120;
    }
      
    int tmp = i;
    while (tmp != 0) {
      tmp = tmp >> 8;
      len--;
    }

    buf.put((byte)len);
      
    len = (len < -120) ? -(len + 120) : -(len + 112);
      
    for (int idx = len; idx != 0; idx--) {
      int shiftbits = (idx - 1) * 8;
      long mask = 0xFFL << shiftbits;
      buf.put((byte)((i & mask) >> shiftbits));
    }
  }
  

  /**
   * Reads a zero-compressed encoded integer from a byte buffer and returns it.
   * @param buf Input byte buffer
   * @throws java.io.IOException 
   * @return deserialized int from stream.
   */
  public static int readVInt(ByteBuffer buf) throws IOException {
    byte firstByte = buf.get();
    int len = decodeVIntSize(firstByte);
    if (len == 1) {
      return firstByte;
    }
    long i = 0;
    for (int idx = 0; idx < len-1; idx++) {
      byte b = buf.get();
      i = i << 8;
      i = i | (b & 0xFF);
    }
    return (int)((isNegativeVInt(firstByte) ? (i ^ -1L) : i));
  }

 
  /**
   * Given the first byte of a vint/vlong, determine the sign
   * @param value the first byte
   * @return is the value negative
   */
  public static boolean isNegativeVInt(byte value) {
    return value < -120 || (value >= -112 && value < 0);
  }

  /**
   * Parse the first byte of a vint/vlong to determine the number of bytes
   * @param value the first byte of the vint/vlong
   * @return the total number of bytes (1 to 9)
   */
  public static int decodeVIntSize(byte value) {
    if (value >= -112) {
      return 1;
    } else if (value < -120) {
      return -119 - value;
    }
    return -111 - value;
  }

  /**
   * Get the encoded length if an integer is stored in a variable-length format
   * @return the encoded length 
   */
  public static int getVIntSize(long i) {
    if (i >= -112 && i <= 127) {
      return 1;
    }
      
    if (i < 0) {
      i ^= -1L; // take one's complement'
    }
    // find the number of bytes with non-leading zeros
    int dataBits = Long.SIZE - Long.numberOfLeadingZeros(i);
    // find the number of data bytes + length byte
    return (dataBits + 7) / 8 + 1;
  }

  public String toString() {
      convert_buffers_to_strings();
      return "row=\"" + row + "\" column=\"" + column_family + ":" + column_qualifier;
  }

  private boolean m_dirty = true;
  private ByteBuffer m_input_buffer;
  private ByteBuffer m_output_buffer;

  private byte [] row_buffer;
  private int row_buffer_offset;
  private int row_buffer_length;

  private byte [] column_family_buffer;
  private int column_family_buffer_offset;
  private int column_family_buffer_length;

  private byte [] column_qualifier_buffer;
  private int column_qualifier_buffer_offset;
  private int column_qualifier_buffer_length;

}
