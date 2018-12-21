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

package org.hypertable.Common;

import java.io.File;
import java.util.StringTokenizer;

public class System {

  public static int HT_DIRECT_IO_ALIGNMENT = 512;

  /** Installation directory */
  public static String installDir;

  /** Cluster definition */
  public static ClusterDefinition clusterDef;

  /** Processor count */
  public static int processorCount;

  static {
	installDir = java.lang.System.getProperty("ht_home");
    if (installDir.endsWith("/"))
        installDir = installDir.substring(0, installDir.length()-1);
    java.lang.System.out.println("Installation Directory = '"
                                 + installDir + "'");

    clusterDef = new ClusterDefinition(installDir + "/conf/cluster.def");

    // Figure out the number of cpus
    processorCount = Runtime.getRuntime().availableProcessors();
  }

  public static void main(String [] args) {
    java.lang.System.out.println("Installation Directory = '"
                                 + installDir + "'");
  }


}

