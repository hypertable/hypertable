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

package org.hypertable.examples.PerformanceTest;

import java.util.LinkedList;

import org.hypertable.thriftgen.*;
import org.hypertable.thrift.SerializedCellsWriter;

public class DriverThreadState {
  Thread thread;
  DriverCommon common;
  LinkedList<SerializedCellsWriter> updates = new LinkedList<SerializedCellsWriter>();
  boolean finished = false;
  double cum_latency;
  double cum_sq_latency;
  double min_latency;
  double max_latency;
}