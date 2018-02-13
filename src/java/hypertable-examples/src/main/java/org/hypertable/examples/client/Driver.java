/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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

package org.hypertable.examples;

import java.util.List;

import org.hypertable.thrift.ThriftClient;
import org.hypertable.thriftgen.*;

public class Driver  {

  static String usage[] = {
    "",
    "usage: Driver <namespace> <command> [ <args> ... ]",
    "",
    "commands:",
    "  get-splits <table> Fetch table splits for table <table>",
    "  hql <hql-command>   Execute <hql-command> and display results",
    "",
    null
  };

  public static void DumpUsageAndExit() {
    java.lang.System.out.println();
    for (int i = 0; usage[i] != null; i++)
      java.lang.System.out.println(usage[i]);
    java.lang.System.out.println();
    java.lang.System.exit(0);
  }


  public static void main(String [] args) {
    long namespace = 0;
    if (args.length < 2)
      DumpUsageAndExit();
    try {
      ThriftClient client = ThriftClient.create("localhost", 15867);
      namespace = client.open_namespace(args[0]);
      if (args.length < 3) {
        client.close_namespace(namespace);
        DumpUsageAndExit();
      }

      if (args[1].equals("get-splits")) {
        List<TableSplit> splits = client.get_table_splits(namespace, args[2]);
        for (final TableSplit s : splits) {
          System.out.println(s);
        }
      }
      else if (args[1].equals("hql")) {
        System.out.println( client.hql_query(namespace, args[2]) );
      }
      else {
        client.close_namespace(namespace);
        DumpUsageAndExit();
      }
      client.close_namespace(namespace);
    } catch (Exception e) {
      e.printStackTrace();
    }
  }
}
