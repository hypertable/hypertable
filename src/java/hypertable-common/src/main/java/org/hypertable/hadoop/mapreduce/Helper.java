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

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.io.Writable;
import org.apache.hadoop.io.WritableComparable;


/**
 * Utility for {@link TableMapper} and {@link TableReducer}
 */
@SuppressWarnings("unchecked")
public class Helper {

  /**
   * Helper method for setting up mapper job.  Sets input format class,
   * output key class, output value class, and mapper class.  Writes
   * input namespace & table name and scan specification into configuation.
   *
   * @param namespace The HT namespace containing table
   * @param table  The table name to read from.
   * @param scan_spec  The scan specification (e.g. query predicate)
   * @param mapper  The mapper class to use.
   * @param outputKeyClass  The class of the output key.
   * @param outputValueClass  The class of the output value.
   * @param job  The current job to adjust.
   * @throws IOException When setting up the details fails.
   * @see InputFormat#TABLE
   * @see InputFormat#SCAN_SPEC
   */
  public static void initMapperJob(String namespace, String table, ScanSpec scan_spec,
      Class<? extends Mapper> mapper,
      Class<? extends WritableComparable> outputKeyClass,
      Class<? extends Writable> outputValueClass, Job job) throws IOException {
    job.setInputFormatClass(InputFormat.class);
    if (outputValueClass != null)
      job.setMapOutputValueClass(outputValueClass);
    if (outputKeyClass != null)
      job.setMapOutputKeyClass(outputKeyClass);
    job.setMapperClass(mapper);
    job.getConfiguration().set(InputFormat.NAMESPACE, namespace);
    job.getConfiguration().set(InputFormat.TABLE, table);
    job.getConfiguration().set(InputFormat.SCAN_SPEC, scan_spec.toSerializedText());
  }

  /**
   * Helper method for setting up reducer job.  Sets output format class,
   * reducer class, and sets output key and value classes to KeyWritable
   * and BytesWritable, respectively.  Writes output namespace and table name into
   * configuration.
   *
   * @param namespace The HT namespace containing the output table
   * @param table The output table.
   * @param reducer  The reducer class to use.
   * @param job  The current job to adjust.
   * @throws IOException When determining the region count fails.
   * @see OutputFormat#NAMESPACE
   * @see OutputFormat#TABLE
   */
  public static void initReducerJob(String namespace, String table,
    Class<? extends Reducer> reducer, Job job) throws IOException {
    job.setOutputFormatClass(OutputFormat.class);
    if (reducer != null)
      job.setReducerClass(reducer);
    job.getConfiguration().set(OutputFormat.NAMESPACE, namespace);
    job.getConfiguration().set(OutputFormat.TABLE, table);
    job.setOutputKeyClass(KeyWritable.class);
    job.setOutputValueClass(BytesWritable.class);
  }

}
