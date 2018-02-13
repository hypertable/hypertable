/*
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

import java.io.IOException;
import java.util.Properties;

import org.apache.hadoop.fs.Path;
import org.apache.hadoop.hive.ql.exec.FileSinkOperator.RecordWriter;
import org.apache.hadoop.hive.ql.io.HiveOutputFormat;
import org.apache.hadoop.io.Writable;
import org.apache.hadoop.io.NullWritable;
import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.util.Progressable;

import org.hypertable.hadoop.util.Row;
import org.hypertable.hadoop.mapred.RowOutputFormat;

/**
 * HiveHypertableOutputFormat implements HiveOutputFormat for HT tables.
 */
public class HiveHypertableOutputFormat extends RowOutputFormat implements
    HiveOutputFormat<NullWritable, Row> {

  /**
   * Update to the final out table, and output an empty key as the key.
   *
   * @param jc
   *          the job configuration file
   * @param finalOutPath
   *          the final output table name
   * @param valueClass
   *          the value class used for create
   * @param isCompressed
   *          whether the content is compressed or not
   * @param tableProperties
   *          the tableInfo of this file's corresponding table
   * @param progress
   *          progress used for status report
   * @return the RecordWriter for the output file
   */
  @Override
  public RecordWriter getHiveRecordWriter(JobConf jc, Path finalOutPath,
      Class<? extends Writable> valueClass, boolean isCompressed,
      Properties tableProperties, Progressable progress) throws IOException {

    String tableName = Utilities.getTableName(jc.get(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_NAME));
    String namespace = Utilities.getNamespace(jc.get(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_NAME));
    jc.set(RowOutputFormat.TABLE, tableName);
    jc.set(RowOutputFormat.NAMESPACE, namespace);

    final org.apache.hadoop.mapred.RecordWriter<
      NullWritable, Row> tblWriter =
      this.getRecordWriter(null, jc, null, progress);

    return new RecordWriter() {

      @Override
      public void close(boolean abort) throws IOException {
        tblWriter.close(null);
      }

      @Override
      public void write(Writable w) throws IOException {
        Row row = (Row) w;
        tblWriter.write(null, row);
      }
    };
  }

}
