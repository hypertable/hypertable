/**
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is distributed under the Apache Software License
 * (http://www.apache.org/licenses/)
 */


package org.hypertable.hadoop.util;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

import java.nio.ByteBuffer;

import java.util.Map;
import java.util.NavigableMap;
import java.util.TreeMap;
import java.util.NavigableSet;
import java.util.TreeSet;
import java.util.Arrays;

import org.apache.hadoop.io.RawComparator;
import org.apache.hadoop.io.Writable;

import org.hypertable.hadoop.util.Serialization;
import org.hypertable.thrift.SerializedCellsReader;
import org.hypertable.Common.ColumnNameSplit;

public class Row implements Writable, Comparable<Row> {

  private ByteBuffer mSerializedCells=null;
  private byte [] mRowKey=null;
  private NavigableMap<byte[], NavigableMap<byte[], byte[]>> mColFamilyToCol=null;
  private SerializedCellsReader mReader = new SerializedCellsReader();

  /** Default Ctor **/
  public Row() {
  }

  /**
   * Constructs a Row from a buffer that can be read via a SerializedCellsReader
   * @param buf byte array that can be read via SerializedCellsReader
   */
  public Row(byte [] buf) {
    mSerializedCells = ByteBuffer.wrap(buf);
    mSerializedCells.rewind();
    mSerializedCells.mark();
    mReader.reset(mSerializedCells);
    if (mReader.next())
      mRowKey = mReader.get_row();
  }

  /**
   * Constructs a Row from a byte buffer that can be read via a SerializedCellsReader
   * @param buf byte array that can be read via SerializedCellsReader
   */
  public Row(ByteBuffer buf) {
    mSerializedCells = buf;
    mReader.reset(mSerializedCells);
    if (mReader.next())
      mRowKey = mReader.get_row();
  }

  /**
   * Set the value of this row to another
   *
   */
  public void set(Row other) {
    mSerializedCells = other.mSerializedCells;
    mReader.reset(mSerializedCells);
    mRowKey = other.mRowKey;
    mColFamilyToCol = other.mColFamilyToCol;
  }

  /**
   * Returns the underlying serialized cells byte array
   * @return serialized cells byte array
   */
  public byte [] getSerializedRow() {
    assert mSerializedCells.arrayOffset() == 0;
    return mSerializedCells.array();
  }

  /**
   * Get the rowkey for this row
   * @return rowkey
   */
  public byte [] getRowKey() {
    return mRowKey;
  }

  /**
   * Returns the value for a cell given the column family and qualifier
   * @param family column family
   * @param qualifier column qualifier
   * @return a map with key,val = col qualifier, cell value.
   */
  public byte [] getValue(byte [] family, byte [] qualifier) {
    if (mColFamilyToCol == null)
      initMap();
    NavigableMap<byte[], byte[]> qualifierToValue = mColFamilyToCol.get(family);

    if(qualifierToValue == null)
      return null;
    return qualifierToValue.get(qualifier);
  }

  /**
   * Returns a map with key,val = col qualifier, cell value.
   * @param family column family
   * @return a map with key,val = col qualifier, cell value.
   */
  public NavigableMap<byte[], byte[]> getColFamilyMap(byte [] family) {
    if (mColFamilyToCol == null)
      initMap();
    return mColFamilyToCol.get(family);
  }

  /**
   * Returns a map with key,val = col family, mapping from col qualifier to value.
   *
   * @return a map with key,val = col family, mapping from col qualifier to value.
   */
  public NavigableMap<byte [], NavigableMap<byte[], byte[]>> getMap() {
    if (mColFamilyToCol == null)
      initMap();
    return mColFamilyToCol;
  }

  /**
   * Returns true if the Row contains this column family
   * @param col column family
   * @return true if the Row contains this qualified column
   */
  public boolean containsColFamily(byte [] cf) {
    if (mColFamilyToCol == null)
      initMap();

    return mColFamilyToCol.containsKey(cf);
  }

  /**
   * Returns true if the Row contains this qualified column
   * @param col column family + qualified column
   * @return true if the Row contains this qualified column
   */
  public boolean containsCol(String col) {
    byte [] col_family=new byte[0];
    byte [] col_qualifier=new byte[0];
    ColumnNameSplit.split(col, col_family, col_qualifier);

    // map shd have the cf and cq
    if (containsColFamily(col_family))
      if (mColFamilyToCol.get(col_family).containsKey(col_qualifier))
        return true;

    return false;
  }

  /**
   * Returns true if the Row contains this qualified column
   * @param family column family
   * @param qualifier column qualifier must not be null
   * @return true if the Row contains this qualified column
   */
  public boolean containsCol(byte []family, byte []qualifier ) {
    if (qualifier == null)
      return false;
    // map shd have the cf and cq
    if (containsColFamily(family))
      if (mColFamilyToCol.get(family).containsKey(qualifier))
        return true;

    return false;
  }

  /**
   * Returns the value for a cell given the qualified column as a string
   * @param col qualified column
   * @return value of the cell
   */
  public byte [] getValue(String col) {
    byte [] cf=new byte[0];
    byte [] cq=new byte[0];

    ColumnNameSplit.split(col, cf, cq);

    return getValue(cf, cq);
  }

  /**
   * Construct 2-level map, mapping each column family to a map of column qualifers, values
   */
  private synchronized void initMap() {
    if (mColFamilyToCol != null || mSerializedCells==null)
      return;

    byte family[];

    mReader.reset(mSerializedCells);
    mColFamilyToCol = new TreeMap<byte[], NavigableMap<byte[], byte[]>>
                          (Serialization.ByteArrayComparator.GET);

    while (mReader.next()) {
      // we're only interested in cells with row = mRowKey
      if (mRowKey == null)
        mRowKey = mReader.get_row();
      if (!Arrays.equals(mReader.get_row(), mRowKey))
        break;

      family = mReader.get_column_family();
      NavigableMap<byte[], byte[]> qualifierToValue = mColFamilyToCol.get(family);
      if (qualifierToValue == null) {
        qualifierToValue = new TreeMap<byte[], byte[]>(Serialization.ByteArrayComparator.GET);
        mColFamilyToCol.put(family, qualifierToValue);
      }
      qualifierToValue.put(mReader.get_column_qualifier(), mReader.get_value());
    }
  }

  //Writable:
  /**
   * Reads the values of each field.
   *
   * @param in  The input to read from.
   * @throws IOException When reading the input fails.
   */
  public void readFields(final DataInput in) throws IOException {
    mRowKey = Serialization.readByteArray(in);
    mSerializedCells = ByteBuffer.wrap(Serialization.readByteArray(in));
    mSerializedCells.mark();
    mReader.reset(mSerializedCells);
  }

  /**
   * Writes the field values to the output.
   *
   * @param out  The output to write to.
   * @throws IOException When writing the values to the output fails.
   */
  public void write(final DataOutput out) throws IOException {
    Serialization.writeByteArray(out, mRowKey);
    assert mSerializedCells.arrayOffset() == 0;
    Serialization.writeByteArray(out, mSerializedCells.array());
  }

  //Comparable
  /**
   * Compares this row against the given one.
   *
   * @param row The split to compare to.
   * @return The result of the comparison.
   * @see java.lang.Comparable#compareTo(java.lang.Object)
   */
  public int compareTo(Row row) {
    return Serialization.compareTo(mRowKey, row.mRowKey);
  }

  /**
   * Render row as a string
   *
   * @return The stringified row.
   */
  public String toString() {

    StringBuilder sb = new StringBuilder();

    if (mSerializedCells == null)
      return sb.toString();
    String bytes;

    sb.append("row=");
    bytes = new String(mRowKey);
    sb.append(bytes);
    sb.append(", cells={");

    if (mColFamilyToCol == null)
      initMap();

    for (Map.Entry<byte[], NavigableMap<byte[], byte[]>> entry : mColFamilyToCol.entrySet()) {
      bytes = new String(entry.getKey());
      String column = " <column=" + bytes + ":";
      for (Map.Entry<byte[], byte[]> ee : entry.getValue().entrySet()) {
        bytes = new String(ee.getKey());
        sb.append(column + bytes);
        sb.append(", valueLen=" + ee.getValue().length + ">");
      }
    }

    sb.append("}");
    return sb.toString();
  }


}
