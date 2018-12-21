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

import java.util.List;
import java.util.Properties;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.hive.serde.serdeConstants;
import org.apache.hadoop.hive.serde2.SerDeException;
import org.apache.hadoop.hive.serde2.lazy.LazySimpleSerDe;
import org.apache.hadoop.hive.serde2.lazy.LazySimpleSerDe.SerDeParameters;
import org.apache.hadoop.hive.serde2.typeinfo.TypeInfo;
import org.apache.hadoop.util.ReflectionUtils;

import org.hypertable.hadoop.hive.ColumnMappings.ColumnMapping;

/**
 * HypertableSerDeParameters encapsulates SerDeParameters and additional configurations that are specific for
 * HypertableSerDe.
 *
 */
public class HypertableSerDeParameters {

  private final SerDeParameters serdeParams;

  private final Configuration job;
  private final Properties tbl;

  private final String columnMappingString;
  private final ColumnMappings columnMappings;

  private final long putTimestamp;
  private final HypertableKeyFactory keyFactory;

  HypertableSerDeParameters(Configuration job, Properties tbl, String serdeName) throws SerDeException {
    this.job = job;
    this.tbl = tbl;
    this.serdeParams = LazySimpleSerDe.initSerdeParams(job, tbl, serdeName);
    this.putTimestamp = Long.valueOf(tbl.getProperty(org.hypertable.hadoop.hive.Properties.HYPERTABLE_PUT_TIMESTAMP, "-1"));

    // Read configuration parameters
    columnMappingString = tbl.getProperty(org.hypertable.hadoop.hive.Properties.HYPERTABLE_COLUMNS_MAPPING);
    // Parse and initialize the Hypertable columns mapping
    columnMappings = ColumnMappings.parseColumnsMapping(columnMappingString);
    columnMappings.setHiveColumnDescription(serdeName, serdeParams.getColumnNames(), serdeParams.getColumnTypes());

    // Precondition: make sure this is done after the rest of the SerDe initialization is done.
    String hypertableTableStorageType = tbl.getProperty(org.hypertable.hadoop.hive.Properties.HYPERTABLE_TABLE_DEFAULT_STORAGE_TYPE);
    columnMappings.parseColumnStorageTypes(hypertableTableStorageType);

    // Build the type property string if not supplied
    String columnTypeProperty = tbl.getProperty(serdeConstants.LIST_COLUMN_TYPES);
    if (columnTypeProperty == null) {
      tbl.setProperty(serdeConstants.LIST_COLUMN_TYPES, columnMappings.toTypesString());
    }

    this.keyFactory = initKeyFactory(job, tbl);
  }

  private HypertableKeyFactory initKeyFactory(Configuration conf, Properties tbl) throws SerDeException {
    try {
      HypertableKeyFactory keyFactory = createKeyFactory(conf, tbl);
      if (keyFactory != null) {
        keyFactory.init(this, tbl);
      }
      return keyFactory;
    } catch (Exception e) {
      throw new SerDeException(e);
    }
  }

  private static HypertableKeyFactory createKeyFactory(Configuration job, Properties tbl) throws Exception {
    return new DefaultHypertableKeyFactory();
  }

  public List<String> getColumnNames() {
    return serdeParams.getColumnNames();
  }

  public List<TypeInfo> getColumnTypes() {
    return serdeParams.getColumnTypes();
  }

  public SerDeParameters getSerdeParams() {
    return serdeParams;
  }

  public long getPutTimestamp() {
    return putTimestamp;
  }

  public int getKeyIndex() {
    return columnMappings.getKeyIndex();
  }

  public ColumnMapping getKeyColumnMapping() {
    return columnMappings.getKeyMapping();
  }

  public ColumnMappings getColumnMappings() {
    return columnMappings;
  }

  public HypertableKeyFactory getKeyFactory() {
    return keyFactory;
  }

  public Configuration getBaseConfiguration() {
    return job;
  }

  public TypeInfo getTypeForName(String columnName) {
    List<String> columnNames = serdeParams.getColumnNames();
    List<TypeInfo> columnTypes = serdeParams.getColumnTypes();
    for (int i = 0; i < columnNames.size(); i++) {
      if (columnName.equals(columnNames.get(i))) {
        return columnTypes.get(i);
      }
    }
    throw new IllegalArgumentException("Invalid column name " + columnName);
  }

  public String toString() {
    return "[" + columnMappingString + ":" + getColumnNames() + ":" + getColumnTypes() + "]";
  }
}
