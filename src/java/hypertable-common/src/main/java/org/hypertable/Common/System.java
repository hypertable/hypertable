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
    String classpath = java.lang.System.getProperty("java.class.path");

    StringTokenizer st = new StringTokenizer(classpath, ":");
    while (st.hasMoreTokens()) {
      String path = st.nextToken();
      if (!path.endsWith(".jar")) {
        File base = new File(path);
        File binDir = new File(base, "bin");
        if (!binDir.exists())
          continue;
        File confDir = new File(base, "conf");
        if (!confDir.exists())
          continue;
        if (path.endsWith("/"))
          installDir = path.substring(0, path.length()-1);
        else
          installDir = path;
        break;
      }
    }

    clusterDef = new ClusterDefinition(installDir + "/conf/cluster.def");

    // Figure out the number of cpus
    processorCount = Runtime.getRuntime().availableProcessors();
  }

  public static void main(String [] args) {
    java.lang.System.out.println("Installation Directory = '"
                                 + installDir + "'");
  }


}

