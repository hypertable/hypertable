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

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInput;
import java.io.DataInputStream;
import java.io.DataOutput;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UnsupportedEncodingException;

import java.util.ArrayList;

import org.hypertable.hadoop.util.Base64;
import org.hypertable.hadoop.util.Serialization;

import org.hypertable.thriftgen.RowInterval;
import org.hypertable.thriftgen.CellInterval;
import org.hypertable.thriftgen.ColumnPredicate;
import org.hypertable.thriftgen.ColumnPredicateOperation;

public class ScanSpec extends org.hypertable.thriftgen.ScanSpec {

  public ScanSpec() {
    super();
  }
  
  /**
   * Performs a deep copy on <i>other</i>.
   */
  public ScanSpec(ScanSpec other) {
    super(other);
  }

  /**
   * Deserializes scan spec from DataInput.
   *
   * @param in DataInput object
   */
  public void readFields(final DataInput in) throws IOException {
    byte [] byte_value;
    boolean isset;
    int len;
    /** row_intervals **/
    isset = in.readBoolean();
    if (isset) {
      setRow_intervalsIsSet(true);
      len = in.readInt();
      if (len > 0) {
        RowInterval ri = null;
        row_intervals = new ArrayList<RowInterval>(len);
        for (int i = 0; i < len; i++) {
          ri = new RowInterval();
          ri.start_row = new String(Serialization.readByteArray(in), "UTF-8");
          ri.start_inclusive = in.readBoolean();
          ri.end_row = new String(Serialization.readByteArray(in), "UTF-8");
          ri.end_inclusive = in.readBoolean();
          row_intervals.add(ri);
        }
      }
    }
    /** cell_intervals **/
    isset = in.readBoolean();
    if (isset) {
      setCell_intervalsIsSet(true);
      len = in.readInt();
      if (len > 0) {
        CellInterval ci = null;
        cell_intervals = new ArrayList<CellInterval>(len);
        for (int i = 0; i < len; i++) {
          ci = new CellInterval();
          ci.start_row = new String(Serialization.readByteArray(in), "UTF-8");
          ci.start_column = new String(Serialization.readByteArray(in), "UTF-8");
          ci.start_inclusive = in.readBoolean();
          ci.end_row = new String(Serialization.readByteArray(in), "UTF-8");
          ci.end_column = new String(Serialization.readByteArray(in), "UTF-8");
          ci.end_inclusive = in.readBoolean();
          cell_intervals.add(ci);
        }
      }
    }
    /** columns **/
    isset = in.readBoolean();
    if (isset) {
      setColumnsIsSet(true);
      len = in.readInt();
      if (len > 0) {
        columns = new ArrayList<String>(len);
        for (int i = 0; i < len; i++)
          columns.add(new String(Serialization.readByteArray(in), "UTF-8"));
      }
    }
    /** return_deletes **/
    isset = in.readBoolean();
    if (isset) {
      setReturn_deletesIsSet(true);
      return_deletes = in.readBoolean();
    }
    /** versions **/
    isset = in.readBoolean();
    if (isset) {
      setVersionsIsSet(true);
      versions = in.readInt();
    }
    /** row_limit **/
    isset = in.readBoolean();
    if (isset) {
      setRow_limitIsSet(true);
      row_limit = in.readInt();
    }
    /** start_time **/
    isset = in.readBoolean();
    if (isset) {
      setStart_timeIsSet(true);
      start_time = in.readLong();
    }
    else
      start_time = Long.MIN_VALUE;
    /** end_time **/
    isset = in.readBoolean();
    if (isset) {
      setEnd_timeIsSet(true);
      end_time = in.readLong();
    }
    else
      end_time = Long.MAX_VALUE;
    /** keys_only **/
    isset = in.readBoolean();
    if (isset) {
      setKeys_onlyIsSet(true);
      keys_only = in.readBoolean();
    }
    /** cell_limit **/
    isset = in.readBoolean();
    if (isset) {
      setCell_limitIsSet(true);
      cell_limit = in.readInt();
    }
    /** cell_limit_per_family **/
    isset = in.readBoolean();
    if (isset) {
      setCell_limit_per_familyIsSet(true);
      cell_limit_per_family = in.readInt();
    }
    /** row_offset **/
    isset = in.readBoolean();
    if (isset) {
      setRow_offsetIsSet(true);
      row_offset = in.readInt();
    }
    /** cell_offset **/
    isset = in.readBoolean();
    if (isset) {
      setCell_offsetIsSet(true);
      cell_offset = in.readInt();
    }
    /** scan_and_filter_rows **/
    isset = in.readBoolean();
    if (isset) {
      setScan_and_filter_rowsIsSet(true);
      scan_and_filter_rows = in.readBoolean();
    }
    /** row_regexp **/
    isset = in.readBoolean();
    if (isset) {
      setRow_regexpIsSet(true);
      row_regexp = new String(Serialization.readByteArray(in), "UTF-8");
    }
    /** value_regexp **/
    isset = in.readBoolean();
    if (isset) {
      setValue_regexpIsSet(true);
      value_regexp = new String(Serialization.readByteArray(in), "UTF-8");
    }
    /** column_predicates **/
    isset = in.readBoolean();
    if (isset) {
      setColumn_predicatesIsSet(true);
      len = in.readInt();
      if (len > 0) {
        column_predicates = new ArrayList<ColumnPredicate>(len);
        for (int i = 0; i < len; i++) {
          ColumnPredicate cp = new ColumnPredicate();
          cp.column_family = new String(Serialization.readByteArray(in), "UTF-8");
          cp.setColumn_familyIsSet(true);
          cp.value = new String(Serialization.readByteArray(in), "UTF-8");
          cp.setValueIsSet(true);
          cp.operation = in.readInt();
          cp.setOperationIsSet(true);
          column_predicates.add(cp);
        }
      }
    }
  }

  /**
   * Serializes scan spec to DataOutput.
   *
   * @param out DataOutput object
   */
  public void write(final DataOutput out) throws IOException {
    byte [] empty = new byte [0];
    try {
      /** row_intervals **/
      if (isSetRow_intervals()) {
        out.writeBoolean(true);
        out.writeInt(row_intervals.size());
        for (final RowInterval ri : row_intervals) {
          if (ri.isSetStart_row())
            Serialization.writeByteArray(out, ri.start_row.getBytes("UTF-8"));
          else
            Serialization.writeByteArray(out, empty);
          if (ri.isSetStart_inclusive())
            out.writeBoolean(ri.start_inclusive);
          else
            out.writeBoolean(true);
          if (ri.isSetEnd_row())
            Serialization.writeByteArray(out, ri.end_row.getBytes("UTF-8"));
          else
            Serialization.writeByteArray(out, empty);
          if (ri.isSetEnd_inclusive())
            out.writeBoolean(ri.end_inclusive);
          else
            out.writeBoolean(true);
        }
      }
      else
        out.writeBoolean(false);
      /** cell_intervals **/
      if (isSetCell_intervals()) {
        out.writeBoolean(true);
        out.writeInt(cell_intervals.size());
        for (final CellInterval ci : cell_intervals) {
          if (ci.isSetStart_row())
            Serialization.writeByteArray(out, ci.start_row.getBytes("UTF-8"));
          else
            Serialization.writeByteArray(out, empty);
          if (ci.isSetStart_column())
            Serialization.writeByteArray(out, ci.start_column.getBytes("UTF-8"));
          else
            Serialization.writeByteArray(out, empty);
          if (ci.isSetStart_inclusive())
            out.writeBoolean(ci.start_inclusive);
          else
            out.writeBoolean(true);
          if (ci.isSetEnd_row())
            Serialization.writeByteArray(out, ci.end_row.getBytes("UTF-8"));
          else
            Serialization.writeByteArray(out, empty);
          if (ci.isSetEnd_column())
            Serialization.writeByteArray(out, ci.end_column.getBytes("UTF-8"));
          else
            Serialization.writeByteArray(out, empty);
          if (ci.isSetEnd_inclusive())
            out.writeBoolean(ci.end_inclusive);
          else
            out.writeBoolean(true);
        }
      }
      else
        out.writeBoolean(false);
      /** colunns **/
      if (isSetColumns()) {
        out.writeBoolean(true);
        out.writeInt(columns.size());
        for (final String s : columns)
          Serialization.writeByteArray(out, s.getBytes("UTF-8"));
      }
      else
        out.writeBoolean(false);
      /** return_deletes **/
      if (isSetReturn_deletes()) {
        out.writeBoolean(true);
        out.writeBoolean(return_deletes);
      }
      else
        out.writeBoolean(false);
      /** versions **/
      if (isSetVersions()) {
        out.writeBoolean(true);
        out.writeInt(versions);
      }
      else
        out.writeBoolean(false);
      /** row_limit **/
      if (isSetRow_limit()) {
        out.writeBoolean(true);
        out.writeInt(row_limit);
      }
      else
        out.writeBoolean(false);
      /** start_time **/
      if (isSetStart_time()) {
        out.writeBoolean(true);
        out.writeLong(start_time);
      }
      else
        out.writeBoolean(false);
      /** end_time **/
      if (isSetEnd_time()) {
        out.writeBoolean(true);
        out.writeLong(end_time);
      }
      else
        out.writeBoolean(false);
      /** keys_only **/
      if (isSetKeys_only()) {
        out.writeBoolean(true);
        out.writeBoolean(keys_only);
      }
      else
        out.writeBoolean(false);
      /** cell_limit **/
      if (isSetCell_limit()) {
        out.writeBoolean(true);
        out.writeInt(cell_limit);
      }
      else
        out.writeBoolean(false);
      /** cell_limit_per_family **/
      if (isSetCell_limit_per_family()) {
        out.writeBoolean(true);
        out.writeInt(cell_limit_per_family);
      }
      else
        out.writeBoolean(false);
      /** row_offset **/
      if (isSetRow_offset()) {
        out.writeBoolean(true);
        out.writeInt(row_offset);
      }
      else
        out.writeBoolean(false);
      /** cell_offset **/
      if (isSetCell_offset()) {
        out.writeBoolean(true);
        out.writeInt(cell_offset);
      }
      else
        out.writeBoolean(false);
      /** scan_and_filter_rows **/
      if (isSetScan_and_filter_rows()) {
        out.writeBoolean(true);
        out.writeBoolean(scan_and_filter_rows);
      }
      else
        out.writeBoolean(false);
      /** row_regexp **/
      if (isSetRow_regexp()) {
        out.writeBoolean(true);
        Serialization.writeByteArray(out, row_regexp.getBytes("UTF-8"));
      }
      else
        out.writeBoolean(false);
      /** value_regexp **/
      if (isSetValue_regexp()) {
        out.writeBoolean(true);
        Serialization.writeByteArray(out, value_regexp.getBytes("UTF-8"));
      }
      else
        out.writeBoolean(false);
      /** column_predicates **/
      if (isSetColumn_predicates()) {
        out.writeBoolean(true);
        out.writeInt(column_predicates.size());
        for (final ColumnPredicate cp : column_predicates) {
          if (cp.isSetColumn_family())
            Serialization.writeByteArray(out, cp.column_family.getBytes("UTF-8"));
          else
            Serialization.writeByteArray(out, empty);
          if (cp.isSetValue())
            Serialization.writeByteArray(out, cp.value.getBytes("UTF-8"));
          else
            Serialization.writeByteArray(out, empty);
          out.writeInt(cp.operation);
        }
      }
      else
        out.writeBoolean(false);
    }
    catch (UnsupportedEncodingException e) {
      e.printStackTrace();
      System.exit(-1);
    }
  }

  /**
   * Returns base 64 encoded, serialized representation of this
   * object, suitable for storing in the configuration object.
   *
   * @return base 64 encoded string
   */
  public String toSerializedText() throws IOException {
    ByteArrayOutputStream out = new ByteArrayOutputStream();  
    DataOutputStream dos = new DataOutputStream(out);
    write(dos);
    return Base64.encodeBytes(out.toByteArray());

  }

  /**
   * Constructs a scan spec object by deserializing base 64 encoded scan spec.
   *
   * @param serializedText base 64 encoded scan spec
   * @return materialized object
   */
  public static ScanSpec serializedTextToScanSpec(String serializedText) throws IOException {
    ScanSpec scan_spec = new ScanSpec();
    if (serializedText != null) {
      ByteArrayInputStream bis = new ByteArrayInputStream(Base64.decode(serializedText));
      DataInputStream dis = new DataInputStream(bis);
      scan_spec.readFields(dis);
    }
    return scan_spec;
  }

  /**
   * A main function which executes tests
   */
  public static void main(String args[]) {
    // initialize a ScanSpec object
    ScanSpec src, dest = null;

    src = new ScanSpec();

    ArrayList<RowInterval> row_intervals = new ArrayList<RowInterval>(2);
    RowInterval ri = new RowInterval();
    ri.start_row = "start1";
    ri.start_inclusive = true;
    ri.end_row = "end1";
    ri.end_inclusive = true;
    row_intervals.add(ri);
    ri = new RowInterval();
    ri.start_row = "start2";
    ri.setStart_inclusive(false);
    ri.end_row = "end2";
    ri.setEnd_inclusive(false);
    row_intervals.add(ri);
    src.setRow_intervals(row_intervals);

    ArrayList<CellInterval> cell_intervals = new ArrayList<CellInterval>(2);
    CellInterval ci = new CellInterval();
    ci.start_row = "start_row1";
    ci.start_column = "start_col1";
    ci.start_inclusive = true;
    ci.end_row = "end_row1";
    ci.end_column = "end_col1";
    ci.end_inclusive = true;
    cell_intervals.add(ci);
    ci = new CellInterval();
    ci.start_row = "start_row2";
    ci.start_column = "start_col2";
    ci.setStart_inclusive(false);
    ci.end_row = "end_row2";
    ci.end_column = "end_col2";
    ci.setEnd_inclusive(false);
    cell_intervals.add(ci);
    src.setCell_intervals(cell_intervals);

    src.setReturn_deletes(true);
    src.setVersions(3);
    src.setRow_limit(4);
    src.setStart_time(5);
    src.setEnd_time(6);
    ArrayList<String> columns = new ArrayList<String>(3);
    columns.add("col1");
    columns.add("col2");
    columns.add("col3");
    src.setColumns(columns);
    src.setKeys_only(true);
    src.setCell_limit(6);
    src.setCell_limit_per_family(7);
    src.setRow_offset(8);
    src.setCell_offset(9);
    src.setScan_and_filter_rows(true);
    src.setRow_regexp("aaa");
    src.setValue_regexp("bbb");

    ArrayList<ColumnPredicate> predicates = new ArrayList<ColumnPredicate>(2);
    ColumnPredicate cp = new ColumnPredicate();
    cp.column_family = "col1";
    cp.value = "val1";
    cp.operation = 1;
    predicates.add(cp);
    cp = new ColumnPredicate();
    cp.column_family = "col2";
    cp.value = "val2";
    cp.operation = 2;
    predicates.add(cp);
    src.setColumn_predicates(predicates);

    try {
      String s = src.toSerializedText();
      dest = ScanSpec.serializedTextToScanSpec(s);
    }
    catch (IOException e) {
      e.printStackTrace();
      System.exit(-1);
    }

    assert dest.row_intervals.size() == 2;
    assert dest.isSetRow_intervals();
    ri = dest.row_intervals.get(0);
    assert ri.start_row.equals("start1");
    assert ri.isSetStart_row();
    assert ri.start_inclusive == true;
    assert ri.end_row.equals("end1");
    assert ri.isSetEnd_row();
    assert ri.end_inclusive == true;
    ri = dest.row_intervals.get(1);
    assert ri.start_row.equals("start2");
    assert ri.isSetStart_row();
    assert ri.start_inclusive == false;
    assert ri.end_row.equals("end2");
    assert ri.isSetEnd_row() == true;
    assert ri.end_inclusive == false;

    assert dest.cell_intervals.size() == 2;
    assert dest.isSetCell_intervals();

    ci = dest.cell_intervals.get(0);
    assert ci.start_row.equals("start_row1");
    assert ci.start_column.equals("start_col1");
    assert ci.start_inclusive == true;
    assert ci.end_row.equals("end_row1");
    assert ci.end_column.equals("end_col1");
    assert ci.end_inclusive == true;
    ci = dest.cell_intervals.get(1);
    assert ci.start_row.equals("start_row2");
    assert ci.start_column.equals("start_col2");
    assert ci.start_inclusive == false;
    assert ci.end_row.equals("end_row2");
    assert ci.end_column.equals("end_col2");
    assert ci.end_inclusive == false;

    assert dest.isSetReturn_deletes();
    assert dest.return_deletes == true;
    assert dest.isSetVersions();
    assert dest.versions == 3;
    assert dest.isSetRow_limit();
    assert dest.row_limit == 4;
    assert dest.isSetStart_time();
    assert dest.start_time == 5;
    assert dest.isSetEnd_time();
    assert dest.end_time == 6;
    assert dest.isSetColumns();
    assert dest.columns.size() == 3;
    assert dest.columns.get(0).equals("col1");
    assert dest.columns.get(1).equals("col2");
    assert dest.columns.get(2).equals("col3");
    assert dest.isSetKeys_only();
    assert dest.keys_only == true;
    assert dest.isSetCell_limit();
    assert dest.cell_limit == 6;
    assert dest.isSetCell_limit_per_family();
    assert dest.cell_limit_per_family == 7;
    assert dest.isSetRow_offset();
    assert dest.row_offset == 8;
    assert dest.isSetCell_offset();
    assert dest.cell_offset == 9;
    assert dest.isSetScan_and_filter_rows();
    assert dest.scan_and_filter_rows == true;
    assert dest.isSetRow_regexp();
    assert dest.row_regexp.equals("aaa");
    assert dest.isSetValue_regexp();
    assert dest.value_regexp.equals("bbb");

    assert dest.column_predicates.size() == 2;
    assert dest.isSetColumn_predicates();

    cp = dest.column_predicates.get(0);
    assert cp.column_family.equals("col1");
    assert cp.value.equals("val1");
    assert cp.operation == 1;
    cp = dest.column_predicates.get(1);
    assert cp.column_family.equals("col2");
    assert cp.value.equals("val2");
    assert cp.operation == 2;

    System.exit(0);
  }
}
