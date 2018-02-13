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
 * Extends the base <code>Mapper</code> class to add the required input key 
 * and value classes.
 * 
 * @param <KEYOUT>  The type of the key.
 * @param <VALUEOUT>  The type of the value.
 * @see org.apache.hadoop.mapreduce.Mapper
 */
public abstract class Mapper<KEYOUT, VALUEOUT>
extends org.apache.hadoop.mapreduce.Mapper<KeyWritable, BytesWritable, KEYOUT, VALUEOUT> {

}
