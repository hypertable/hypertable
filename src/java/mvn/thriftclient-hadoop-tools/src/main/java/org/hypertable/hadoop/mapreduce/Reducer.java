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

import org.apache.hadoop.io.BytesWritable;

/**
 * Extends the basic <code>Reducer</code> class to add the required key and
 * value input/output classes. While the input key and value as well as the 
 * output key can be anything handed in from the previous map phase the output 
 * value <u>must</u> be either a {@link org.apache.hadoop.hbase.client.Put Put} 
 * or a {@link org.apache.hadoop.hbase.client.Delete Delete} instance when
 * using the {@link TableOutputFormat} class.
 * <p>
 * This class is extended by {@link IdentityTableReducer} but can also be 
 * subclassed to implement similar features or any custom code needed. It has
 * the advantage to enforce the output value to a specific basic type. 
 * 
 * @param <KEYIN>  The type of the input key.
 * @param <VALUEIN>  The type of the input value.
 * @see org.apache.hadoop.mapreduce.Reducer
 */
public abstract class Reducer<KEYIN, VALUEIN>
extends org.apache.hadoop.mapreduce.Reducer<KEYIN, VALUEIN, KeyWritable, BytesWritable> {
}