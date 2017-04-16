package org.hypertable.hadoop.pig;

import java.io.IOException;
import java.io.DataInput;
import java.io.DataOutput;
import java.lang.reflect.Field;
import java.util.Properties;
// import java.math.BigDecimal;
// import java.math.BigInteger;
import java.util.Map;

import org.apache.hadoop.conf.Configuration;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.mapreduce.InputSplit;
import org.apache.hadoop.mapreduce.InputFormat;
import org.apache.hadoop.mapreduce.OutputFormat;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.RecordReader;
import org.apache.hadoop.mapreduce.RecordWriter;
import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.io.WritableComparable;

import org.apache.pig.LoadCaster;
import org.apache.pig.LoadStoreCaster;
import org.apache.pig.LoadFunc;
import org.apache.pig.OrderedLoadFunc;
import org.apache.pig.StoreFuncInterface;
import org.apache.pig.ResourceSchema;
import org.apache.pig.builtin.Utf8StorageConverter;
import org.apache.pig.ResourceSchema.ResourceFieldSchema;
import org.apache.pig.backend.hadoop.executionengine.mapReduceLayer.PigSplit;
import org.apache.pig.data.DataByteArray;
import org.apache.pig.data.DataBag;
import org.apache.pig.data.DataType;
import org.apache.pig.data.Tuple;
import org.apache.pig.data.TupleFactory;
import org.apache.pig.impl.util.ObjectSerializer;
import org.apache.pig.impl.util.UDFContext;

import org.joda.time.DateTime;

import org.hypertable.hadoop.mapreduce.ScanSpec;
import org.hypertable.hadoop.mapreduce.KeyWritable;
import org.hypertable.hadoop.mapreduce.TableSplit;
// ScanSpec donor
import org.hypertable.hadoop.mapred.TextTableInputFormat;


public class HypertableStorage extends LoadFunc implements StoreFuncInterface, OrderedLoadFunc {
    private static final Log LOG = LogFactory.getLog(HypertableStorage.class);

    private RecordReader reader;
    private RecordWriter writer;
    private String contextSignature = null;

    private LoadCaster caster_;

    private ResourceSchema schema_;

    private String columns;
    private String valueRegexps;
    private String columnPredicates;
    private String options;
    private String rowInterval;
    private String timestampInterval;

    public HypertableStorage() throws IOException {
        this(null);
    }

    public HypertableStorage(String columns)
        throws IOException {
        this(columns, null);
    }

    public HypertableStorage(String columns, String rowInterval)
        throws IOException {
        this(columns, rowInterval, null);
    }

    public HypertableStorage(String columns, String rowInterval, String options)
        throws IOException {
        this(columns, rowInterval, options, null, null, null);
    }

    public HypertableStorage(String columns, String rowInterval, String options,
                             String valueRegexps, String columnPredicates,
                             String timestampInterval)
        throws IOException {
        caster_ = new Utf8StorageConverter();

        this.columns = columns;
        this.valueRegexps = valueRegexps;
        this.columnPredicates = columnPredicates;
        this.options = options;
        this.rowInterval = rowInterval;
        this.timestampInterval = timestampInterval;
    }

    @Override
    public InputFormat getInputFormat() throws IOException {
        return new org.hypertable.hadoop.mapreduce.InputFormat();
    }

    @Override
    public void setLocation(String location, Job job) throws IOException {
        Properties udfProps = getUDFProperties();
        Configuration jobConf = job.getConfiguration();
        jobConf.setBoolean("pig.noSplitCombination", true);

        String namespaceAndTable = location;
        if (location.startsWith("hypertable://")) {
            namespaceAndTable = location.substring(13);
        }

        String delimiter = "/";
        int delimiterIndex = namespaceAndTable.lastIndexOf(delimiter);
        String namespace = namespaceAndTable.substring(0, delimiterIndex);
        String table = namespaceAndTable.substring(delimiterIndex + 1);
        ScanSpec scanSpec = configureScanSpec(job);

        jobConf.set(org.hypertable.hadoop.mapreduce.InputFormat.NAMESPACE, namespace);
        jobConf.set(org.hypertable.hadoop.mapreduce.InputFormat.TABLE, table);
        jobConf.set(org.hypertable.hadoop.mapreduce.InputFormat.SCAN_SPEC, scanSpec.toSerializedText());
    }

    private ScanSpec configureScanSpec(Job job) {
        Configuration jobConf = job.getConfiguration();
        JobConf localConf = new JobConf(jobConf);
        if (columns != null && !columns.isEmpty())
            localConf.set(TextTableInputFormat.COLUMNS, columns);
        if (valueRegexps != null && !valueRegexps.isEmpty())
            localConf.set(TextTableInputFormat.VALUE_REGEXPS, valueRegexps);
        if (columnPredicates != null && !columnPredicates.isEmpty())
            localConf.set(TextTableInputFormat.COLUMN_PREDICATES, columnPredicates);
        if (options != null && !options.isEmpty())
            localConf.set(TextTableInputFormat.OPTIONS, options);
        if (rowInterval != null && !rowInterval.isEmpty())
            localConf.set(TextTableInputFormat.ROW_INTERVAL, rowInterval);
        if (timestampInterval != null && !timestampInterval.isEmpty())
            localConf.set(TextTableInputFormat.TIMESTAMP_INTERVAL, timestampInterval);

        TextTableInputFormat ttif = new TextTableInputFormat();
        ttif.configure(localConf);

        try {
            Field f = TextTableInputFormat.class.getDeclaredField("m_base_spec");
            f.setAccessible(true);
            ScanSpec m_base_spec = (ScanSpec) f.get(ttif);
            return m_base_spec;
        } catch (Exception e) {
            LOG.error("Unable to configure scan spec.");
            e.printStackTrace();
        }
        return null;
    }

    @Override
    public void prepareToRead(RecordReader reader, PigSplit split) {
        this.reader = reader;
    }

    @Override
    public void setUDFContextSignature(String signature) {
        this.contextSignature = signature;
    }

    @Override
    public Tuple getNext() throws IOException {
        try {
            if (reader.nextKeyValue()) {
                KeyWritable rowKey = (KeyWritable) reader.getCurrentKey();
                BytesWritable cell = (BytesWritable) reader.getCurrentValue();

                int tupleSize = 4;

                Tuple tuple = TupleFactory.getInstance().newTuple(tupleSize);

                // Key
                tuple.set(0, rowKey.getRow());
                tuple.set(1, rowKey.getColumn_family());
                tuple.set(2, rowKey.getColumn_qualifier());
                // Cell
                tuple.set(3, new DataByteArray(cell.getBytes()));

                if (LOG.isDebugEnabled()) {
                    for (int i = 0; i < tuple.size(); i++) {
                        LOG.debug("tuple value:" + tuple.get(i));
                    }
                }

                return tuple;
            }
        } catch (InterruptedException e) {
            throw new IOException(e);
        }
        return null;
    }

    /**
     * Returns UDFProperties based on <code>contextSignature</code>.
     */
    private Properties getUDFProperties() {
        return UDFContext.getUDFContext()
            .getUDFProperties(this.getClass(), new String[] {contextSignature});
    }

    @Override
    public OutputFormat getOutputFormat() throws IOException {
        return new org.hypertable.hadoop.mapreduce.OutputFormat();
    }

    @Override
    public void setStoreLocation(String location, Job job) throws IOException {
        Configuration jobConf = job.getConfiguration();

        String namespaceAndTable = location;
        if (location.startsWith("hypertable://")) {
            namespaceAndTable = location.substring(13);
        }

        String delimiter = "/";
        int delimiterIndex = namespaceAndTable.lastIndexOf(delimiter);
        String namespace = namespaceAndTable.substring(0, delimiterIndex);
        String table = namespaceAndTable.substring(delimiterIndex + 1);

        jobConf.set(org.hypertable.hadoop.mapreduce.OutputFormat.NAMESPACE, namespace);
        jobConf.set(org.hypertable.hadoop.mapreduce.OutputFormat.TABLE, table);

        String serializedSchema = getUDFProperties().getProperty(contextSignature + "_schema");
        if (serializedSchema!= null) {
            schema_ = (ResourceSchema) ObjectSerializer.deserialize(serializedSchema);
        }
    }

    @Override
    public String relToAbsPathForStoreLocation(String location, Path curDir)
        throws IOException {
        return location;
    }

    @Override
    public LoadCaster getLoadCaster() throws IOException {
        return caster_;
    }

    @Override
    public void prepareToWrite(RecordWriter writer) throws IOException {
        this.writer = writer;
    }

    @Override
    public void setStoreFuncUDFContextSignature(String signature) {
        this.contextSignature = signature;
    }

    @Override
    public void putNext(Tuple t) throws IOException {
        byte[] rowValue = getBytesForField(t, 0);
        byte[] columnFamilyValue = getBytesForField(t, 1);
        byte[] columnQualifierValue = getBytesForField(t, 2);

        KeyWritable key = new KeyWritable();
        key.setRow(rowValue, 0, rowValue.length);
        key.setColumn_family(columnFamilyValue, 0, columnFamilyValue.length);
        key.setColumn_qualifier(columnQualifierValue, 0, columnQualifierValue.length);

        byte[] cellValue = getBytesForField(t, 3);
        BytesWritable cell = new BytesWritable(cellValue);

        try {
            writer.write(key, cell);
        } catch (InterruptedException e) {
            throw new IOException(e);
        }
    }

    private byte[] getBytesForField(Tuple t, int fieldNumber) throws IOException {
        ResourceFieldSchema[] fieldSchemas = (schema_ == null) ? null : schema_.getFields();
        byte fieldType = (fieldSchemas == null) ? DataType.findType(t.get(fieldNumber)) : fieldSchemas[fieldNumber].getType();
        byte[] fieldValue = objToBytes(t.get(fieldNumber), fieldType);
        return fieldValue;
    }

    @SuppressWarnings("unchecked")
    private byte[] objToBytes(Object o, byte type) throws IOException {
        LoadStoreCaster caster = (LoadStoreCaster) caster_;
        if (o == null) return null;
        switch (type) {
        case DataType.BYTEARRAY: return ((DataByteArray) o).get();
        case DataType.BAG: return caster.toBytes((DataBag) o);
        case DataType.CHARARRAY: return caster.toBytes((String) o);
        case DataType.DOUBLE: return caster.toBytes((Double) o);
        case DataType.FLOAT: return caster.toBytes((Float) o);
        case DataType.INTEGER: return caster.toBytes((Integer) o);
        case DataType.LONG: return caster.toBytes((Long) o);
	// Pig 0.12.0 required
        // case DataType.BIGINTEGER: return caster.toBytes((BigInteger) o);
        // case DataType.BIGDECIMAL: return caster.toBytes((BigDecimal) o);
        case DataType.BOOLEAN: return caster.toBytes((Boolean) o);
        case DataType.DATETIME: return caster.toBytes((DateTime) o);

        // The type conversion here is unchecked.
        // Relying on DataType.findType to do the right thing.
        case DataType.MAP: return caster.toBytes((Map<String, Object>) o);

        case DataType.NULL: return null;
        case DataType.TUPLE: return caster.toBytes((Tuple) o);
        case DataType.ERROR: throw new IOException("Unable to determine type of " + o.getClass());
        default: throw new IOException("Unable to find a converter for tuple field " + o);
        }
    }

    @Override
    public void checkSchema(ResourceSchema s) throws IOException {
        schema_ = s;
        getUDFProperties().setProperty(contextSignature + "_schema",
                                       ObjectSerializer.serialize(schema_));
    }

    @Override
    public void cleanupOnFailure(String location, Job job) throws IOException {
    }

    @Override
    public void cleanupOnSuccess(String location, Job job) throws IOException {
    }

    /**
     * OrderedLoadFunc Methods.
     */
    @Override
    public WritableComparable<InputSplit> getSplitComparable(InputSplit split)
        throws IOException {
        return new WritableComparable<InputSplit>() {
            TableSplit tsplit = new TableSplit();

            @Override
            public void readFields(DataInput in) throws IOException {
                tsplit.readFields(in);
            }

            @Override
            public void write(DataOutput out) throws IOException {
                tsplit.write(out);
            }

            @Override
            public int compareTo(InputSplit split) {
                return tsplit.compareTo((TableSplit) split);
            }
        };
    }
}
