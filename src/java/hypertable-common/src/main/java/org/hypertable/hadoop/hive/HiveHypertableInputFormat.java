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
import java.util.Collections;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.hive.ql.index.IndexPredicateAnalyzer;
import org.apache.hadoop.hive.serde2.ColumnProjectionUtils;
import org.apache.hadoop.hive.serde2.SerDeException;
import org.apache.hadoop.hive.serde2.typeinfo.TypeInfo;
import org.apache.hadoop.hive.shims.ShimLoader;
import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.mapred.FileInputFormat;
import org.apache.hadoop.mapred.InputFormat;
import org.apache.hadoop.mapred.InputSplit;
import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.mapred.JobConfigurable;
import org.apache.hadoop.mapred.RecordReader;
import org.apache.hadoop.mapred.Reporter;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.JobContext;

import org.hypertable.hadoop.hive.ColumnMappings.ColumnMapping;
import org.hypertable.hadoop.mapred.RowInputFormat;
import org.hypertable.hadoop.mapred.TableSplit;
import org.hypertable.hadoop.mapreduce.ScanSpec;
import org.hypertable.hadoop.util.Row;
import org.hypertable.thriftgen.ClientException;

/**
 * HiveHypertableInputFormat implements InputFormat for Hypertable storage handler
 * tables, decorating an underlying Hypertable RowInputFormat with extra Hive logic
 * such as column pruning.
 */
public class HiveHypertableInputFormat<K extends BytesWritable, V extends Row>
    implements InputFormat<K, V> {

  static final Log LOG = LogFactory.getLog(HiveHypertableInputFormat.class);


  public HiveHypertableInputFormat() {
  }

  @Override
  public RecordReader<K, V> getRecordReader(InputSplit split, JobConf jobConf,
                                         Reporter reporter) throws IOException {
    HiveHypertableSplit htSplit = (HiveHypertableSplit)split;

    String namespace = Utilities.getNamespace(jobConf.get(Properties.HYPERTABLE_TABLE_NAME));
    String tableName = Utilities.getTableName(jobConf.get(Properties.HYPERTABLE_TABLE_NAME));
    String columnsMappingSpec = jobConf.get(Properties.HYPERTABLE_COLUMNS_MAPPING);
    ColumnMappings columnMappings;

    try {
      columnMappings = ColumnMappings.parseColumnsMapping(columnsMappingSpec);
    } catch (SerDeException se) {
      throw new IOException(se);
    }

    List<Integer> readColIDs = ColumnProjectionUtils.getReadColumnIDs(jobConf);
    if (columnMappings.size() < readColIDs.size()) {
      throw new IOException("Cannot read more columns than the given table contains.");
    }

    ScanSpec scanSpec = new ScanSpec();

    boolean readAllColumns = ColumnProjectionUtils.isReadAllColumns(jobConf);
    scanSpec.setKeys_only(true);

    // The list of families that have been added to the scan
    List<String> addedFamilies = new ArrayList<String>();

    if (!readAllColumns) {
      ColumnMapping[] columnsMapping = columnMappings.getColumnsMapping();
      for (int i : readColIDs) {
        ColumnMapping colMap = columnsMapping[i];
        if (colMap.isRowKey) {
          continue;
        }
        if (colMap.qualifierName == null) {
          scanSpec.addToColumns(colMap.familyName);
          addedFamilies.add(colMap.familyName);
        }
        else {
          if (!addedFamilies.contains(colMap.familyName)) {
            String column = colMap.familyName + ":" + colMap.qualifierName;
            scanSpec.addToColumns(column);
          }
        }
        scanSpec.setKeys_only(false);
      }
    }

    scanSpec.setVersions(1);

    ScanSpec spec = htSplit.getSplit().createScanSpec(scanSpec);

    RowInputFormat rif = new RowInputFormat();
    rif.set_scan_spec(spec);
    rif.set_namespace(namespace);
    rif.set_table_name(tableName);

    return (RecordReader<K, V>)
      rif.getRecordReader(htSplit.getSplit(), jobConf, reporter);
  }

  static IndexPredicateAnalyzer newIndexPredicateAnalyzer(
      String keyColumnName, TypeInfo keyColType, boolean isKeyBinary) {
    return newIndexPredicateAnalyzer(keyColumnName, keyColType.getTypeName(), isKeyBinary);
  }

  /**
   * Instantiates a new predicate analyzer suitable for
   * determining how to push a filter down into the Hypertable scan,
   * based on the rules for what kinds of pushdown we currently support.
   *
   * @param keyColumnName name of the Hive column mapped to the Hypertable row key
   *
   * @return preconfigured predicate analyzer
   */
  static IndexPredicateAnalyzer newIndexPredicateAnalyzer(
    String keyColumnName, String keyColType, boolean isKeyBinary) {

    IndexPredicateAnalyzer analyzer = new IndexPredicateAnalyzer();

    // We can always do equality predicate. Just need to make sure we get appropriate
    // BA representation of constant of filter condition.
    analyzer.addComparisonOp("org.apache.hadoop.hive.ql.udf.generic.GenericUDFOPEqual");
    // We can do other comparisons only if storage format in hypertable is either binary
    // or we are dealing with string types since there lexographic ordering will suffice.
    if(isKeyBinary || (keyColType.equalsIgnoreCase("string"))){
      analyzer.addComparisonOp("org.apache.hadoop.hive.ql.udf.generic." +
        "GenericUDFOPEqualOrGreaterThan");
      analyzer.addComparisonOp("org.apache.hadoop.hive.ql.udf.generic.GenericUDFOPEqualOrLessThan");
      analyzer.addComparisonOp("org.apache.hadoop.hive.ql.udf.generic.GenericUDFOPLessThan");
      analyzer.addComparisonOp("org.apache.hadoop.hive.ql.udf.generic.GenericUDFOPGreaterThan");
    }

    // and only on the key column
    analyzer.clearAllowedColumnNames();
    analyzer.allowColumnName(keyColumnName);

    return analyzer;
  }


  @Override
  public InputSplit[] getSplits(JobConf jobConf, int numSplits) throws IOException {

    String namespace = Utilities.getNamespace(jobConf.get(Properties.HYPERTABLE_TABLE_NAME));
    String tableName = Utilities.getTableName(jobConf.get(Properties.HYPERTABLE_TABLE_NAME));
    String columnsMappingSpec = jobConf.get(Properties.HYPERTABLE_COLUMNS_MAPPING);

    if (columnsMappingSpec == null) {
      throw new IOException("hypertable.columns.mapping required for Hypertable Table.");
    }

    ColumnMappings columnMappings = null;
    try {
      columnMappings = ColumnMappings.parseColumnsMapping(columnsMappingSpec);
    } catch (SerDeException e) {
      throw new IOException(e);
    }

    int iKey = columnMappings.getKeyIndex();
    ColumnMapping keyMapping = columnMappings.getKeyMapping();

    RowInputFormat rif = new RowInputFormat();
    rif.set_namespace(namespace);
    rif.set_table_name(tableName);

    ScanSpec scanSpec = new ScanSpec();

    boolean readAllColumns = ColumnProjectionUtils.isReadAllColumns(jobConf);
    scanSpec.setKeys_only(true);

    // The list of families that have been added to the scan
    List<String> addedFamilies = new ArrayList<String>();

    if (!readAllColumns) {
      for (ColumnMapping colMap : columnMappings) {
        if (colMap.isRowKey) {
          continue;
        }
        if (colMap.qualifierName == null) {
          scanSpec.addToColumns(colMap.familyName);
          addedFamilies.add(colMap.familyName);
        }
        else {
          if (!addedFamilies.contains(colMap.familyName)) {
            String column = colMap.familyName + ":" + colMap.qualifierName;
            scanSpec.addToColumns(column);
          }
        }
        scanSpec.setKeys_only(false);
      }
    }

    scanSpec.setVersions(1);

    rif.set_scan_spec(scanSpec);

    Path [] tablePaths = FileInputFormat.getInputPaths(jobConf);

    int num_splits=0;
    InputSplit [] splits = rif.getSplits(jobConf, num_splits);
    InputSplit [] results = new InputSplit[splits.length];
    for (int ii=0; ii< splits.length; ii++) {
      results[ii] = new HiveHypertableSplit((TableSplit) splits[ii], tablePaths[0]);
    }
    return results;
  }

}
