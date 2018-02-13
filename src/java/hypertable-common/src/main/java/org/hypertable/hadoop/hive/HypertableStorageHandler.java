/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.hypertable.hadoop.hive;

import org.hypertable.thriftgen.*;

import org.apache.thrift.transport.TTransportException;
import org.apache.thrift.TException;

import org.hypertable.hadoop.hive.ColumnMappings.ColumnMapping;
import org.hypertable.hadoop.mapred.RowOutputFormat;
import org.hypertable.thrift.ThriftClient;
import org.hypertable.thriftgen.ClientException;;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Properties;
import java.util.Set;

import org.apache.commons.io.IOUtils;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.hive.conf.HiveConf;
import org.apache.hadoop.hive.metastore.HiveMetaHook;
import org.apache.hadoop.hive.metastore.MetaStoreUtils;
import org.apache.hadoop.hive.metastore.api.MetaException;
import org.apache.hadoop.hive.metastore.api.Table;
import org.apache.hadoop.hive.metastore.api.hive_metastoreConstants;
import org.apache.hadoop.hive.ql.index.IndexPredicateAnalyzer;
import org.apache.hadoop.hive.ql.index.IndexSearchCondition;
import org.apache.hadoop.hive.ql.metadata.DefaultStorageHandler;
import org.apache.hadoop.hive.ql.metadata.HiveStoragePredicateHandler;
import org.apache.hadoop.hive.ql.plan.ExprNodeDesc;
import org.apache.hadoop.hive.ql.plan.ExprNodeGenericFuncDesc;
import org.apache.hadoop.hive.ql.plan.TableDesc;
import org.apache.hadoop.hive.serde2.Deserializer;
import org.apache.hadoop.hive.serde2.SerDe;
import org.apache.hadoop.mapred.InputFormat;
import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.mapred.OutputFormat;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.util.StringUtils;

/**
 * HypertableStorageHandler provides a HiveStorageHandler implementation for
 * Hypertable.
 */
public class HypertableStorageHandler extends DefaultStorageHandler
  implements HiveMetaHook, HiveStoragePredicateHandler {

  final static public String DEFAULT_PREFIX = "default.";

  private long mNamespaceId=0;
  private ThriftClient mClient=null;
  private Configuration mConf=null;

   private String getHTNamespace(Table tbl) {
     // Give preference to TBLPROPERTIES over SERDEPROPERTIES
     // (really we should only use TBLPROPERTIES, so this is just
     // for backwards compatibility with the original specs).
     String hypertableTableName = tbl.getParameters().get(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_NAME);
     if (hypertableTableName == null)
       hypertableTableName = tbl.getSd().getSerdeInfo().getParameters().get(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_NAME);
     return Utilities.getNamespace(hypertableTableName);
  }

  private String getHTTableName(Table tbl) throws TTransportException, TException, ClientException {
    // Give preference to TBLPROPERTIES over SERDEPROPERTIES
    // (really we should only use TBLPROPERTIES, so this is just
    // for backwards compatibility with the original specs).

    if (mClient == null) {
      mClient = ThriftClient.create("localhost", 15867);
      mNamespaceId = mClient.open_namespace(getHTNamespace(tbl));
    }

     String hypertableTableName = tbl.getParameters().get(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_NAME);
     if (hypertableTableName == null)
       hypertableTableName = tbl.getSd().getSerdeInfo().getParameters().get(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_NAME);
     return Utilities.getTableName(hypertableTableName);
  }

  @Override
  public void preDropTable(Table table) throws MetaException {
    // nothing to do
  }

  @Override
  public void rollbackDropTable(Table table) throws MetaException {
    // nothing to do
  }

  @Override
  public void commitDropTable(
    Table tbl, boolean deleteData) throws MetaException {

    try {
      String tableName = getHTTableName(tbl);
      boolean isExternal = MetaStoreUtils.isExternalTable(tbl);
      if (deleteData && !isExternal) {
        mClient.table_drop(mNamespaceId, tableName, true);
      }
      // nothing to do since this is an external table
    } catch (Exception ie) {
      throw new MetaException(StringUtils.stringifyException(ie));
    }
  }

  @Override
  public void preCreateTable(Table tbl) throws MetaException {
    boolean isExternal = MetaStoreUtils.isExternalTable(tbl);

    // We'd like to move this to HiveMetaStore for any non-native table, but
    // first we need to support storing NULL for location on a table
    if (tbl.getSd().getLocation() != null) {
      throw new MetaException("LOCATION may not be specified for Hypertable.");
    }

    try {
      String tableName = getHTTableName(tbl);
      Map<String, String> serdeParam = tbl.getSd().getSerdeInfo().getParameters();
      String hypertableColumnsMapping = serdeParam.get(org.hypertable.hadoop.hive.Properties.HYPERTABLE_COLUMNS_MAPPING);

      ColumnMappings columnMappings = ColumnMappings.parseColumnsMapping(hypertableColumnsMapping);

      if (!mClient.table_exists(mNamespaceId, tableName)) {
        // if it is not an external table then create one
        if (!isExternal) {

          Schema schema = new Schema();
          Map<String,ColumnFamilySpec> column_families = new HashMap<String,ColumnFamilySpec>();
          Set<String> uniqueColumnFamilies = new HashSet<String>();

          for (ColumnMapping colMap : columnMappings) {
            if (!colMap.isRowKey) {
              uniqueColumnFamilies.add(colMap.familyName);
            }
          }

          for (String columnFamily : uniqueColumnFamilies) {
            ColumnFamilySpec cf = new ColumnFamilySpec();
            cf.setName(columnFamily);
            column_families.put(columnFamily, cf);
          }

          schema.setColumn_families(column_families);

          mClient.table_create(mNamespaceId, tableName, schema);
        } else {
          // an external table
          throw new MetaException("Hypertable table " + tableName +
              " doesn't exist while the table is declared as an external table.");
        }

      } else {
        if (!isExternal) {
          throw new MetaException("Table " + tableName + " already exists"
            + " within Hypertable; use CREATE EXTERNAL TABLE instead to"
            + " register it in Hive.");
        }
        // make sure the schema mapping is right
        Schema schema = mClient.get_schema(mNamespaceId, tableName);

        for (ColumnMapping colMap : columnMappings) {
          if (colMap.isRowKey) {
            continue;
          }
          if (!schema.column_families.containsKey(colMap.familyName)) {
            throw new MetaException("Column Family " + colMap.familyName
                + " is not defined in Hypertable table " + tableName);
          }
        }
      }
    } catch (Exception se) {
      throw new MetaException(StringUtils.stringifyException(se));
    }
  }

  @Override
  public void rollbackCreateTable(Table table) throws MetaException {
    try {
      boolean isExternal = MetaStoreUtils.isExternalTable(table);
      String tableName = getHTTableName(table);
      if (!isExternal && mClient.table_exists(mNamespaceId, tableName)) {
        // we have created a Hypertable table, so we delete it to roll back;
        mClient.table_drop(mNamespaceId, tableName, true);
      }
    } catch (Exception ie) {
      throw new MetaException(StringUtils.stringifyException(ie));
    }
  }

  @Override
  public void commitCreateTable(Table table) throws MetaException {
    // nothing to do
  }

  @Override
  public Configuration getConf() {
    return mConf;
  }

  public Configuration getJobConf() {
    return mConf;
  }

  @Override
  public void setConf(Configuration conf) {
    mConf = conf;
  }

  @Override
  public Class<? extends InputFormat> getInputFormatClass() {
    return HiveHypertableInputFormat.class;
  }

  @Override
  public Class<? extends OutputFormat> getOutputFormatClass() {
    return HiveHypertableOutputFormat.class;
  }

  @Override
  public Class<? extends SerDe> getSerDeClass() {
    return HypertableSerDe.class;
  }

  @Override
  public HiveMetaHook getMetaHook() {
    return this;
  }

  @Override
  public void configureInputJobProperties(
    TableDesc tableDesc,
    Map<String, String> jobProperties) {
    configureTableJobProperties(tableDesc, jobProperties);
  }

  @Override
  public void configureOutputJobProperties(
    TableDesc tableDesc,
    Map<String, String> jobProperties) {
    configureTableJobProperties(tableDesc, jobProperties);
  }

  @Override
  public void configureTableJobProperties(
    TableDesc tableDesc,
    Map<String, String> jobProperties) {

    Properties tableProperties = tableDesc.getProperties();

    jobProperties.put(
      org.hypertable.hadoop.hive.Properties.HYPERTABLE_COLUMNS_MAPPING,
      tableProperties.getProperty(org.hypertable.hadoop.hive.Properties.HYPERTABLE_COLUMNS_MAPPING));
    jobProperties.put(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_DEFAULT_STORAGE_TYPE,
      tableProperties.getProperty(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_DEFAULT_STORAGE_TYPE,"string"));

    String hypertableTableName =
      tableProperties.getProperty(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_NAME);
    if (hypertableTableName == null) {
      hypertableTableName =
        tableProperties.getProperty(hive_metastoreConstants.META_TABLE_NAME);
        hypertableTableName = hypertableTableName.toLowerCase();
      if (hypertableTableName.startsWith(DEFAULT_PREFIX)) {
        hypertableTableName = hypertableTableName.substring(DEFAULT_PREFIX.length());
      }
    }
    String tableName = Utilities.getTableName(hypertableTableName);
    jobProperties.put(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_NAME, hypertableTableName);
    jobProperties.put(RowOutputFormat.TABLE, tableName);

    String namespace = Utilities.getNamespace(hypertableTableName);
    jobProperties.put(RowOutputFormat.NAMESPACE, namespace);
  }


  public void configureJobConf(TableDesc tableDesc, JobConf jobConf) {
    try {
      HypertableSerDeParameters serdeParams =
        new HypertableSerDeParameters(jobConf, tableDesc.getProperties(), HypertableSerDe.class.getName());
      serdeParams.getKeyFactory().configureJobConf(tableDesc, jobConf);
      jobConf.set("tmpjars", getClass().getProtectionDomain().getCodeSource().getLocation().getPath());
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Override
  public DecomposedPredicate decomposePredicate(
    JobConf jobConf,
    Deserializer deserializer,
    ExprNodeDesc predicate)
  {
    HypertableKeyFactory keyFactory = ((HypertableSerDe) deserializer).getKeyFactory();
    return keyFactory.decomposePredicate(jobConf, deserializer, predicate);
  }

  public static DecomposedPredicate decomposePredicate(
      JobConf jobConf,
      HypertableSerDe hypertableSerDe,
      ExprNodeDesc predicate) {
    ColumnMapping keyMapping = hypertableSerDe.getHypertableSerdeParam().getKeyColumnMapping();
    IndexPredicateAnalyzer analyzer = HiveHypertableInputFormat.newIndexPredicateAnalyzer(
        keyMapping.columnName, keyMapping.columnType, keyMapping.binaryStorage.get(0));
    List<IndexSearchCondition> searchConditions =
        new ArrayList<IndexSearchCondition>();
    ExprNodeGenericFuncDesc residualPredicate =
        (ExprNodeGenericFuncDesc)analyzer.analyzePredicate(predicate, searchConditions);
    int scSize = searchConditions.size();
    if (scSize < 1 || 2 < scSize) {
      // Either there was nothing which could be pushed down (size = 0),
      // there were complex predicates which we don't support yet.
      // Currently supported are one of the form:
      // 1. key < 20                        (size = 1)
      // 2. key = 20                        (size = 1)
      // 3. key < 20 and key > 10           (size = 2)
      return null;
    }
    if (scSize == 2 &&
        (searchConditions.get(0).getComparisonOp()
        .equals("org.apache.hadoop.hive.ql.udf.generic.GenericUDFOPEqual") ||
        searchConditions.get(1).getComparisonOp()
        .equals("org.apache.hadoop.hive.ql.udf.generic.GenericUDFOPEqual"))) {
      // If one of the predicates is =, then any other predicate with it is illegal.
      return null;
    }

    DecomposedPredicate decomposedPredicate = new DecomposedPredicate();
    decomposedPredicate.pushedPredicate = analyzer.translateSearchConditions(
      searchConditions);
    decomposedPredicate.residualPredicate = residualPredicate;
    return decomposedPredicate;
  }
}
