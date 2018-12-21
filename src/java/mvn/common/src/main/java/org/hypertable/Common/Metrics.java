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

/** @file
 * Declarations for Metrics.
 * This file contains the interface declaration of Metrics, an interface for
 * metrics collection classes.
 */

package org.hypertable.Common;

/** Metrics interface.
 * This interface is for classes that compute metrics and publish them to a
 * metrics collector.
 */
public interface Metrics {

  /** Collects metrics.
   * Computes metrics and publishes them via <code>collector</code>.
   * @param now Current time in milliseconds
   * @param collector Metrics collector
   */
  public void collect(long now, MetricsCollector collector);

}
