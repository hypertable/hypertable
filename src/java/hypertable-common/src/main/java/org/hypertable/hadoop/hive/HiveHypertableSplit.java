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

package org.hypertable.hadoop.hive;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.mapred.FileSplit;

import org.apache.hadoop.mapred.InputSplit;

import org.hypertable.hadoop.mapred.TableSplit;

/**
 * HiveHypertableSplit augments TableSplit
 */
public class HiveHypertableSplit extends FileSplit implements InputSplit {

  private TableSplit split;

  public HiveHypertableSplit() {
    super((Path) null, 0, 0, (String[]) null);
    split = new TableSplit();
  }

  public HiveHypertableSplit(TableSplit split, Path dummyPath) {
    super(dummyPath, 0, 0, (String[]) null);
    this.split = split;
  }

  public TableSplit getSplit() {
    return this.split;
  }

  @Override
  public void readFields(DataInput in) throws IOException {
    super.readFields(in);
    split.readFields(in);
  }

  @Override
  public String toString() {
    return "TableSplit: split=" + split + ", FileSplit=" + super.toString();
  }

  @Override
  public void write(DataOutput out) throws IOException {
    super.write(out);
    split.write(out);
  }

  @Override
  public long getLength() {
    return split.getLength();
  }

  @Override
  public String[] getLocations() throws IOException {
    return split.getLocations();
  }
}
