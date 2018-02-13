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

import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Properties;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.hive.serde.serdeConstants;
import org.apache.hadoop.hive.serde2.AbstractSerDe;
import org.apache.hadoop.hive.serde2.ByteStream;
import org.apache.hadoop.hive.serde2.SerDeException;
import org.apache.hadoop.hive.serde2.SerDeStats;
import org.apache.hadoop.hive.serde2.SerDeUtils;
import org.apache.hadoop.hive.serde2.lazy.LazyFactory;
import org.apache.hadoop.hive.serde2.lazy.LazySimpleSerDe;
import org.apache.hadoop.hive.serde2.lazy.LazySimpleSerDe.SerDeParameters;
import org.apache.hadoop.hive.serde2.lazy.LazyUtils;
import org.apache.hadoop.hive.serde2.lazy.objectinspector.LazySimpleStructObjectInspector;
import org.apache.hadoop.hive.serde2.objectinspector.ListObjectInspector;
import org.apache.hadoop.hive.serde2.objectinspector.MapObjectInspector;
import org.apache.hadoop.hive.serde2.objectinspector.ObjectInspector;
import org.apache.hadoop.hive.serde2.objectinspector.ObjectInspector.Category;
import org.apache.hadoop.hive.serde2.objectinspector.PrimitiveObjectInspector;
import org.apache.hadoop.hive.serde2.objectinspector.StructField;
import org.apache.hadoop.hive.serde2.objectinspector.StructObjectInspector;
import org.apache.hadoop.hive.serde2.objectinspector.primitive.PrimitiveObjectInspectorFactory;
import org.apache.hadoop.hive.serde2.typeinfo.MapTypeInfo;
import org.apache.hadoop.hive.serde2.typeinfo.StructTypeInfo;
import org.apache.hadoop.hive.serde2.typeinfo.TypeInfo;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.hypertable.hadoop.hive.ColumnMappings.ColumnMapping;
import org.hypertable.hadoop.util.Row;
import org.hypertable.thrift.SerializedCellsFlag;
import org.hypertable.thrift.SerializedCellsWriter;


import org.apache.hadoop.io.Writable;

/**
 * HypertableSerDe can be used to serialize object into an Hypertable table and
 * deserialize objects from an Hypertable table.
 */
public class HypertableSerDe extends AbstractSerDe {

  public static final Log LOG = LogFactory.getLog(HypertableSerDe.class);

  private ObjectInspector cachedObjectInspector;
  private boolean doColumnRegexMatching;
  private ColumnMappings columnMappings;
  private HypertableSerDeParameters serdeParams;
  private boolean useJSONSerialize;
  private LazyHypertableRow cachedHypertableRow;
  private final ByteStream.Output serializeStream = new ByteStream.Output();
  private final SerializedCellsWriter cellsWriter = new SerializedCellsWriter(1024, true);
  private Class<?> compositeKeyClass;
  private Object compositeKeyObj;

  // used for serializing a field
  private byte [] separators;     // the separators array
  private boolean escaped;        // whether we need to escape the data when writing out
  private byte escapeChar;        // which char to use as the escape char, e.g. '\\'
  private boolean [] needsEscape; // which chars need to be escaped. This array should have size
                                  // of 128. Negative byte values (or byte values >= 128) are
                                  // never escaped.
  @Override
  public String toString() {
    return serdeParams.toString();
  }

  public HypertableSerDe() throws SerDeException {
  }

  /**
   * Initialize the SerDe given parameters.
   * @see SerDe#initialize(Configuration, Properties)
   */
  @Override
  public void initialize(Configuration conf, Properties tbl)
      throws SerDeException {

    serdeParams = new HypertableSerDeParameters(conf, tbl, getClass().getName());

    separators = serdeParams.getSerdeParams().getSeparators();
    escaped = serdeParams.getSerdeParams().isEscaped();
    escapeChar = serdeParams.getSerdeParams().getEscapeChar();
    needsEscape = serdeParams.getSerdeParams().getNeedsEscape();

    columnMappings = serdeParams.getColumnMappings();

    cachedObjectInspector =
      HypertableLazyObjectFactory.createLazyHypertableStructInspector(
          serdeParams.getSerdeParams(), serdeParams.getKeyIndex(), serdeParams.getKeyFactory());

    cachedHypertableRow =
      new LazyHypertableRow((LazySimpleStructObjectInspector) cachedObjectInspector);

    if (compositeKeyClass != null) {
      // initialize the constructor of the composite key class with its object inspector
      initCompositeKeyClass(conf,tbl);
    }

    if (LOG.isDebugEnabled()) {
      LOG.debug("HypertableSerDe initialized with " + serdeParams.toString());
    }
  }

  public HypertableSerDeParameters getHypertableSerdeParam() {
    return serdeParams;
  }

  /**
   * Deserialize a row from the Hypertable Row writable to a LazyObject
   * @param row the Hypertable Row Writable containing the row
   * @return the deserialized object
   * @see SerDe#deserialize(Writable)
   */
  @Override
  public Object deserialize(Writable row) throws SerDeException {

    if (!(row instanceof Row)) {
      throw new SerDeException(getClass().getName() + ": expects Row!");
    }

    cachedHypertableRow.init((Row)row, columnMappings.getColumnsMapping(), compositeKeyObj);

    return cachedHypertableRow;
  }

  @Override
  public ObjectInspector getObjectInspector() throws SerDeException {
    return cachedObjectInspector;
  }

  @Override
  public Class<? extends Writable> getSerializedClass() {
    return Row.class;
  }

  @Override
  public Writable serialize(Object obj, ObjectInspector objInspector)
      throws SerDeException {
    if (objInspector.getCategory() != Category.STRUCT) {
      throw new SerDeException(getClass().toString()
          + " can only serialize struct types, but we got: "
          + objInspector.getTypeName());
    }

    // Prepare the field ObjectInspectors
    StructObjectInspector soi = (StructObjectInspector) objInspector;
    List<? extends StructField> fields = soi.getAllStructFieldRefs();
    List<Object> list = soi.getStructFieldsDataAsList(obj);
    List<? extends StructField> declaredFields =
      (serdeParams.getSerdeParams().getRowTypeInfo() != null &&
       ((StructTypeInfo) serdeParams.getSerdeParams().getRowTypeInfo())
        .getAllStructFieldNames().size() > 0) ?
      ((StructObjectInspector)getObjectInspector()).getAllStructFieldRefs()
      : null;


    cellsWriter.clear();

    try {

      int keyIndex = serdeParams.getKeyIndex();

      // Serialize key
      serializeStream.reset();
      ObjectInspector foi = fields.get(keyIndex).getFieldObjectInspector();
      Object f = (list == null ? null : list.get(keyIndex));
      boolean isNotNull = serialize(f, foi, 1);
      assert(isNotNull);
      byte [] key = new byte[serializeStream.getCount()];
      System.arraycopy(serializeStream.getData(), 0, key, 0, serializeStream.getCount());

      // Serialize each field
      for (int i = 0; i < fields.size(); i++) {
        if (i == keyIndex) {
          continue;
        }
        serializeField(i, key, fields, list, declaredFields);
      }
    } catch (IOException e) {
      throw new SerDeException(e);
    }

    return new Row( cellsWriter.array() );
  }

  private void serializeField(
    int i,
    byte [] key,
    List<? extends StructField> fields,
    List<Object> list,
    List<? extends StructField> declaredFields) throws IOException {

    // column mapping info
    ColumnMapping colMap = columnMappings.getColumnsMapping()[i];

    // Get the field objectInspector and the field object.
    ObjectInspector foi = fields.get(i).getFieldObjectInspector();
    Object f = (list == null ? null : list.get(i));

    if (f == null) {
      // a null object, we do not serialize it
      return;
    }

    assert(!colMap.isRowKey);

    // If the field corresponds to a column family in Hypertable
    if (colMap.qualifierName == null) {
      MapObjectInspector moi = (MapObjectInspector) foi;
      ObjectInspector koi = moi.getMapKeyObjectInspector();
      ObjectInspector voi = moi.getMapValueObjectInspector();

      Map<?, ?> map = moi.getMap(f);
      if (map == null) {
        return;
      } else {
        for (Map.Entry<?, ?> entry: map.entrySet()) {
          // Get the Key
          serializeStream.reset();

          // Map keys are required to be primitive and may be serialized in binary format
          //boolean isNotNull = serialize(entry.getKey(), koi, 3, colMap.binaryStorage.get(0));

          // Hypertable only allows qualifiers to be strings
          boolean isNotNull = serialize(entry.getKey(), koi, 3);
          if (!isNotNull) {
            continue;
          }

          // Get the column-qualifier
          byte [] columnQualifierBytes = new byte[serializeStream.getCount()];
          System.arraycopy(serializeStream.getData(), 0,
                           columnQualifierBytes, 0, serializeStream.getCount());

          // Get the Value
          serializeStream.reset();

          // Map values may be serialized in binary format when they are primitive and binary
          // serialization is the option selected
          isNotNull = serialize(entry.getValue(), voi, 3, colMap.binaryStorage.get(1));
          if (!isNotNull) {
            continue;
          }
          byte [] value = new byte[serializeStream.getCount()];
          System.arraycopy(serializeStream.getData(), 0, value, 0, serializeStream.getCount());

          cellsWriter.add(key, 0, key.length,
                          colMap.familyNameBytes, 0, colMap.familyNameBytes.length,
                          columnQualifierBytes, 0, columnQualifierBytes.length,
                          (serdeParams.getPutTimestamp() < 0) ? SerializedCellsFlag.AUTO_ASSIGN : serdeParams.getPutTimestamp(),
                          value, 0, value.length, SerializedCellsFlag.FLAG_INSERT);
        }
      }
    } else {
      // If the field that is passed in is NOT a primitive, and either the
      // field is not declared (no schema was given at initialization), or
      // the field is declared as a primitive in initialization, serialize
      // the data to JSON string.  Otherwise serialize the data in the
      // delimited way.
      serializeStream.reset();
      boolean isNotNull;
      if (!foi.getCategory().equals(Category.PRIMITIVE)
          && (declaredFields == null ||
              declaredFields.get(i).getFieldObjectInspector().getCategory()
              .equals(Category.PRIMITIVE) || useJSONSerialize)) {

        // we always serialize the String type using the escaped algorithm for LazyString
        isNotNull = serialize(
            SerDeUtils.getJSONString(f, foi),
            PrimitiveObjectInspectorFactory.javaStringObjectInspector,
            1, false);
      } else {
        // use the serialization option switch to write primitive values as either a variable
        // length UTF8 string or a fixed width bytes if serializing in binary format
        isNotNull = serialize(f, foi, 1, colMap.binaryStorage.get(0));
      }
      if (!isNotNull) {
        return;
      }
      byte [] value = new byte[serializeStream.getCount()];
      System.arraycopy(serializeStream.getData(), 0, value, 0, serializeStream.getCount());
      cellsWriter.add(key, 0, key.length,
                      colMap.familyNameBytes, 0, colMap.familyNameBytes.length,
                      colMap.qualifierNameBytes, 0, colMap.qualifierNameBytes.length,
                      (serdeParams.getPutTimestamp() < 0) ? SerializedCellsFlag.AUTO_ASSIGN : serdeParams.getPutTimestamp(),
                      value, 0, value.length, SerializedCellsFlag.FLAG_INSERT);
    }
  }

  /*
   * Serialize the row into a ByteStream.
   *
   * @param obj           The object for the current field.
   * @param objInspector  The ObjectInspector for the current Object.
   * @param level         The current level of separator.
   * @param writeBinary   Whether to write a primitive object as an UTF8 variable length string or
   *                      as a fixed width byte array onto the byte stream.
   * @throws IOException  On error in writing to the serialization stream.
   * @return true         On serializing a non-null object, otherwise false.
   */
  private boolean serialize(
      Object obj,
      ObjectInspector objInspector,
      int level,
      boolean writeBinary) throws IOException {

    if (objInspector.getCategory() == Category.PRIMITIVE && writeBinary) {
      LazyUtils.writePrimitive(serializeStream, obj, (PrimitiveObjectInspector) objInspector);
      return true;
    } else {
      return serialize(obj, objInspector, level);
    }
  }

  private boolean serialize(
      Object obj,
      ObjectInspector objInspector,
      int level) throws IOException {

    switch (objInspector.getCategory()) {
      case PRIMITIVE: {
        LazyUtils.writePrimitiveUTF8(serializeStream, obj,
            (PrimitiveObjectInspector) objInspector, escaped, escapeChar, needsEscape);
        return true;
      }

      case LIST: {
        char separator = (char) separators[level];
        ListObjectInspector loi = (ListObjectInspector)objInspector;
        List<?> list = loi.getList(obj);
        ObjectInspector eoi = loi.getListElementObjectInspector();
        if (list == null) {
          return false;
        } else {
          for (int i = 0; i < list.size(); i++) {
            if (i > 0) {
              serializeStream.write(separator);
            }
            serialize(list.get(i), eoi, level + 1);
          }
        }
        return true;
      }

      case MAP: {
        char separator = (char) separators[level];
        char keyValueSeparator = (char) separators[level+1];
        MapObjectInspector moi = (MapObjectInspector) objInspector;
        ObjectInspector koi = moi.getMapKeyObjectInspector();
        ObjectInspector voi = moi.getMapValueObjectInspector();

        Map<?, ?> map = moi.getMap(obj);
        if (map == null) {
          return false;
        } else {
          boolean first = true;
          for (Map.Entry<?, ?> entry: map.entrySet()) {
            if (first) {
              first = false;
            } else {
              serializeStream.write(separator);
            }
            serialize(entry.getKey(), koi, level+2);
            serializeStream.write(keyValueSeparator);
            serialize(entry.getValue(), voi, level+2);
          }
        }
        return true;
      }

      case STRUCT: {
        char separator = (char)separators[level];
        StructObjectInspector soi = (StructObjectInspector)objInspector;
        List<? extends StructField> fields = soi.getAllStructFieldRefs();
        List<Object> list = soi.getStructFieldsDataAsList(obj);
        if (list == null) {
          return false;
        } else {
          for (int i = 0; i < list.size(); i++) {
            if (i > 0) {
              serializeStream.write(separator);
            }

            serialize(list.get(i), fields.get(i).getFieldObjectInspector(),
                level + 1);
          }
        }
        return true;
      }
    }

    throw new RuntimeException("Unknown category type: " + objInspector.getCategory());
  }

  /**
   * Initialize the composite key class with the objectinspector for the key
   *
   * @throws SerDeException
   * */
  private void initCompositeKeyClass(Configuration conf,Properties tbl) throws SerDeException {

    int i = 0;

    // find the hypertable row key
    for (ColumnMapping colMap : columnMappings) {
      if (colMap.isRowKey) {
        break;
      }
      i++;
    }

    ObjectInspector keyObjectInspector = ((LazySimpleStructObjectInspector) cachedObjectInspector)
        .getAllStructFieldRefs().get(i).getFieldObjectInspector();

    try {
      compositeKeyObj = compositeKeyClass.getDeclaredConstructor(
            LazySimpleStructObjectInspector.class, Properties.class, Configuration.class)
            .newInstance(
                ((LazySimpleStructObjectInspector) keyObjectInspector), tbl, conf);
    } catch (IllegalArgumentException e) {
      throw new SerDeException(e);
    } catch (SecurityException e) {
      throw new SerDeException(e);
    } catch (InstantiationException e) {
      throw new SerDeException(e);
    } catch (IllegalAccessException e) {
      throw new SerDeException(e);
    } catch (InvocationTargetException e) {
      throw new SerDeException(e);
    } catch (NoSuchMethodException e) {
      // the constructor wasn't defined in the implementation class. Flag error
      throw new SerDeException("Constructor not defined in composite key class ["
          + compositeKeyClass.getName() + "]", e);
    }
  }

  /**
   * @return the useJSONSerialize
   */
  public boolean isUseJSONSerialize() {
    return useJSONSerialize;
  }

  /**
   * @param useJSONSerialize the useJSONSerialize to set
   */
  public void setUseJSONSerialize(boolean useJSONSerialize) {
    this.useJSONSerialize = useJSONSerialize;
  }

  /**
   * @return 0-based offset of the key column within the table
   */
  int getKeyColumnOffset() {
    return serdeParams.getKeyIndex();
  }

  List<Boolean> getStorageFormatOfCol(int colPos){
    return columnMappings.getColumnsMapping()[colPos].binaryStorage;
  }

  @Override
  public SerDeStats getSerDeStats() {
    // no support for statistics
    return null;
  }

  public HypertableKeyFactory getKeyFactory() {
    return serdeParams.getKeyFactory();
  }

}
