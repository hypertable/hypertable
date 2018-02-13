/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is distributed under the Apache Software License
 * (http://www.apache.org/licenses/)
 */

package org.hypertable.thrift;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.HashMap;
import java.util.Iterator;

import org.hypertable.thriftgen.*;

public class BasicClientTest {
  public static void main(String [] args) {
    ThriftClient client = null;
    long ns = -1;
    try {
      client = ThriftClient.create("localhost", 15867);
      ns = client.namespace_open("test");
      // HQL examples
      show(client.hql_query(ns, "show tables").toString());
      show(client.hql_query(ns, "select * from thrift_test").toString());
      // Schema example
      Schema schema = new Schema();
      schema = client.table_get_schema(ns, "thrift_test");

      Iterator ag_it = schema.access_groups.keySet().iterator();
      show("Access groups:");
      while (ag_it.hasNext()) {
        show("\t" + ag_it.next());
      }

      Iterator cf_it = schema.column_families.keySet().iterator();
      show("Column families:");
      while (cf_it.hasNext()) {
        show("\t" + cf_it.next());
      }

      // mutator examples
      long mutator = client.mutator_open(ns, "thrift_test", 0, 0);

      try {
        Cell cell = new Cell();
        Key key = new Key();
        key.setRow("java-k1");
        key.setColumn_family("col");
        cell.setKey(key);
        String vtmp = "java-v1";
        cell.setValue( ByteBuffer.wrap(vtmp.getBytes()) );
        client.mutator_set_cell(mutator, cell);
      }
      finally {
        client.mutator_close(mutator);
      }

      // shared mutator example
      {
        MutateSpec mutate_spec = new MutateSpec();
        mutate_spec.setAppname("test-java");
        mutate_spec.setFlush_interval(1000);
        Cell cell = new Cell();
        Key key;

        key = new Key();
        key.setRow("java-put1");
        key.setColumn_family("col");
        cell.setKey(key);
        String vtmp = "java-put-v1";
        cell.setValue( ByteBuffer.wrap(vtmp.getBytes()) );
        client.offer_cell(ns, "thrift_test", mutate_spec, cell);

        key = new Key();
        key.setRow("java-put2");
        key.setColumn_family("col");
        cell.setKey(key);
        vtmp = "java-put-v2";
        cell.setValue( ByteBuffer.wrap(vtmp.getBytes()) );
        client.shared_mutator_refresh(ns, "thrift_test", mutate_spec);
        client.shared_mutator_set_cell(ns, "thrift_test", mutate_spec, cell);
        Thread.sleep(2000);
      }

      // scanner examples
      System.out.println("Full scan");
      ScanSpec scanSpec = new ScanSpec(); // empty scan spec select all
      long scanner = client.scanner_open(ns, "thrift_test", scanSpec);

      try {
        List<Cell> cells = client.scanner_get_cells(scanner);

        while (cells.size() > 0) {
          for (Cell cell : cells) {
            byte[] tmp = cell.getValue();
            String s = new String(tmp);
            show(s);
          }
          cells = client.scanner_get_cells(scanner);
        }
      }
      finally {
        client.scanner_close(scanner);
      }
      // restricted scanspec
      scanSpec.addToColumns("col:/^.*$/");
      scanSpec.setRow_regexp("java.*");
      scanSpec.setValue_regexp("v2");
      scanner = client.scanner_open(ns, "thrift_test", scanSpec);
      System.out.println("Restricted scan");
      try {
        List<Cell> cells = client.scanner_get_cells(scanner);

        while (cells.size() > 0) {
          for (Cell cell : cells) {
            byte[] tmp = cell.getValue();
            String s = new String(tmp);
            show(s);
          }
          cells = client.scanner_get_cells(scanner);
        }
      }
      finally {
        client.scanner_close(scanner);
      }

      // asynchronous api
      long future=0;
      long mutator_async_1=0;
      long mutator_async_2=0;
      long color_scanner=0;
      long location_scanner=0;
      long energy_scanner=0;
      int expected_cells = 6;
      int num_cells = 0;

      try {
        System.out.println("Asynchronous mutator");
        future = client.future_open(0);
        mutator_async_1 = client.async_mutator_open(ns, "thrift_test", future, 0);
        mutator_async_2 = client.async_mutator_open(ns, "thrift_test", future, 0);
        Result result;

        Cell cell = new Cell();
        Key key;

        key = new Key();
        key.setRow("java-put1");
        key.setColumn_family("col");
        cell.setKey(key);
        String vtmp = "java-async-put-v1";
        cell.setValue( ByteBuffer.wrap(vtmp.getBytes()) );
        client.async_mutator_set_cell(mutator_async_1, cell);

        key = new Key();
        key.setRow("java-put2");
        key.setColumn_family("col");
        cell.setKey(key);
        vtmp = "java-async-put-v2";
        cell.setValue( ByteBuffer.wrap(vtmp.getBytes()) );
        client.async_mutator_set_cell(mutator_async_2, cell);

        client.async_mutator_flush(mutator_async_1);
        client.async_mutator_flush(mutator_async_2);

        int num_flushes=0;
        while (true) {
          result = client.future_get_result(future, 0);
          if (result.is_empty || result.is_error || result.is_scan)
            break;
          num_flushes++;
        }
        if (num_flushes > 2) {
          System.out.println("Expected 2 flushes, received " + num_flushes);
          System.exit(1);
        }
        if (client.future_is_cancelled(future) || client.future_is_full(future) ||
            !client.future_is_empty(future) || client.future_has_outstanding(future)) {
          System.out.println("Future object in unexpected state");
          System.exit(1);
        }
      }
      finally {
        client.async_mutator_close(mutator_async_1);
        client.async_mutator_close(mutator_async_2);
      }

      try {
        System.out.println("Asynchronous scan");
        ScanSpec ss = new ScanSpec();
        color_scanner = client.async_scanner_open(ns, "FruitColor", future, ss);
        location_scanner = client.async_scanner_open(ns, "FruitLocation", future, ss);
        energy_scanner = client.async_scanner_open(ns, "FruitEnergy", future, ss);
        Result result;
        while (true) {
          result = client.future_get_result(future, 0);
          if (result.is_empty || result.is_error || !result.is_scan)
            break;
          for (Cell cell : result.cells) {
            byte[] tmp = cell.getValue();
            String s = new String(tmp);
            show(s);
            num_cells++;
          }
          if (num_cells >= 6) {
            client.future_cancel(future);
            break;
          }
        }
        if (!client.future_is_cancelled(future)) {
          System.out.println("Expected future object to be cancelled");
          System.exit(1);
        }
      }
      finally {
        // This close order should be ok
        client.future_close(future);
        client.async_scanner_close(color_scanner);
        client.async_scanner_close(location_scanner);
        client.async_scanner_close(energy_scanner);
      }
      if (num_cells != 6) {
        System.out.println("Expected " + expected_cells + " cells got " + num_cells);
        System.exit(1);
      }


      // issue 497
      {
        Cell cell;
        Key key;
        String str;

        client.hql_query(ns, "drop table if exists java_thrift_test");
        client.hql_query(ns, "create table java_thrift_test ( c1, c2, c3 )");

        mutator = client.mutator_open(ns, "java_thrift_test", 0, 0);

        cell = new Cell();
        key = new Key();
        key.setRow("000");
        key.setColumn_family("c1");
        key.setColumn_qualifier("test");
        cell.setKey(key);
        str = "foo";
        cell.setValue( ByteBuffer.wrap(str.getBytes()) );
        client.mutator_set_cell(mutator, cell);

        cell = new Cell();
        key = new Key();
        key.setRow("000");
        key.setColumn_family("c1");
        cell.setKey(key);
        str = "bar";
        cell.setValue( ByteBuffer.wrap(str.getBytes()) );
        client.mutator_set_cell(mutator, cell);

        client.mutator_close(mutator);

        HqlResult result = client.hql_query(ns, "select * from java_thrift_test");
        List<Cell> cells = result.cells;
        int qualifier_count = 0;
        for(Cell c:cells) {
          if (c.key.isSetColumn_qualifier() && c.key.column_qualifier.length() == 0)
            qualifier_count++;
        }

        if (qualifier_count != 1) {
          System.out.println("ERROR: Expected qualifier_count of 1, got " + qualifier_count);
          client.namespace_close(ns);
          System.exit(1);
        }
      }

      client.namespace_close(ns);

    }
    catch (Exception e) {
      e.printStackTrace();
      try {
        if (client != null && ns != -1)
          client.namespace_close(ns);
      }
      catch (Exception ce) {
        System.err.println("Problen closing namespace \"test\" - " + e.getMessage());
      }
      System.exit(1);
    }
  }

  private static void show(String line) {
    System.out.println(line);
  }
}
