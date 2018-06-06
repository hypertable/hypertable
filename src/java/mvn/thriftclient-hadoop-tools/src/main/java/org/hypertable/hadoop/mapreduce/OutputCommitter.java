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

import org.apache.hadoop.mapreduce.JobContext;
import org.apache.hadoop.mapreduce.TaskAttemptContext;

/**
 * Dummy class
 */
public class OutputCommitter extends org.apache.hadoop.mapreduce.OutputCommitter {

  @Override
  public void abortTask(TaskAttemptContext arg0) {
  }

  @Deprecated
  public void cleanupJob(JobContext arg0) {
  }

  @Override
  public void commitTask(TaskAttemptContext arg0) {
  }

  @Override
  public boolean needsTaskCommit(TaskAttemptContext arg0) {
    return false;
  }

  @Override
  public void setupJob(JobContext arg0) {
  }

  @Override
  public void setupTask(TaskAttemptContext arg0) {
  }

}
