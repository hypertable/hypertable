
package org.hypertable.test;

import org.hypertable.thrift.SerializedCellsFlag;
import org.hypertable.thrift.SerializedCellsReader;
import org.hypertable.thrift.SerializedCellsWriter;
import org.hypertable.thrift.ThriftClient;
import org.hypertable.thriftgen.RowInterval;
import org.hypertable.thriftgen.ScanSpec;
import org.hypertable.thriftgen.ColumnFamilySpec;
import org.hypertable.thriftgen.AccessGroupSpec;
import org.hypertable.thriftgen.Schema;

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Random;

public class GetCell {
  private static final String NamespaceName = "get_cell_test";
  private static final int ThreadCount = 5;

    private static final String counter_table_schema_xml =
      "<Schema generation=\"1\">\n" +
      "  <AccessGroup name=\"default\">\n" +
      "    <ColumnFamily id=\"1\">\n" +
      "      <Generation>1</Generation>\n" +
      "      <Name>D</Name>\n" +
      "      <Counter>true</Counter>\n" +
      "    </ColumnFamily>\n" +
      "  </AccessGroup>\n" +
      "</Schema>";

  public static void main(String[] args) {
    try {
      final ThriftClient client = ThriftClient.create("localhost", 15867);
      client.namespace_create(NamespaceName);

      final Thread[] threads = new Thread[ThreadCount];
      for (int i = 0; i < ThreadCount; i++) {
        final int id = i;
        threads[i] = new Thread(new Runnable() {
                    @Override
                      public void run() {
                      runTest(Integer.toString(id));
                    }
          });
        threads[i].start();
      }
      for (int i = 0; i < ThreadCount; i++) {
        try {
          threads[i].join();
        } catch (InterruptedException e) {
          e.printStackTrace();
        }
      }

      System.out.println("Success");
    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  private static boolean checkCounter(ThriftClient client, long ns,
                                      String table_name, long expectedValue) {
    boolean ret = true;
    try {
      // read from hypertable, variant 1
      ByteBuffer cellValue1 = client.get_cell(ns, table_name, "A", "D:B");
      long readFromDb1 = Long.parseLong(new String(cellValue1.array(), cellValue1.position(), cellValue1.remaining()));

      // read from hypertable, variant 2
      ByteBuffer serializedCells = client.get_cells_serialized(ns, table_name, new ScanSpec()
                                                                     .setRow_intervals(Arrays.asList(new RowInterval()
                                                                                                     .setStart_row("A").setStart_inclusive(true)
                                                                                                     .setEnd_row("A").setEnd_inclusive(true))));
      SerializedCellsReader reader = new SerializedCellsReader();
      reader.reset(serializedCells);
      reader.next();
      long readFromDb2 = Long.parseLong(new String(reader.get_value()));

      if (readFromDb1 != expectedValue) {
        System.out.println("get_cell() returned " + readFromDb1 + " != " + expectedValue);
        ret = false;
      }

      if (readFromDb2 != expectedValue) {
        System.out.println("get_cells_serialized() returned " + readFromDb2 + " != " + expectedValue);
        ret = false;
      }

    } catch (Exception e) {
      e.printStackTrace();
    }
    return ret;
  }


  private static void runTest(final String id) {
    try {
      final ThriftClient client = ThriftClient.create("localhost", 15867);
      final long ns = client.open_namespace(NamespaceName);
      final String table_name = "table" + id;

      Schema schema = new Schema();
      AccessGroupSpec ag = new AccessGroupSpec();
      ag.setName("default");
      schema.putToAccess_groups(ag.name, ag);
      ColumnFamilySpec cf = new ColumnFamilySpec();
      cf.setName("D");
      cf.options.setCounter(true);
      schema.putToColumn_families(cf.name, cf);

      client.table_create(ns, table_name, schema);

      long expectedValue = 0;
      Random random = new Random();

      for (int i = 0; i < 1000000; i++) {
        if ((i % 1000) == 0) {
          if (i > 0 && !checkCounter(client, ns, table_name, expectedValue))
            System.exit(1);
          System.out.println(id + ": " + i + " iterations done so far. Accumulated value: " + expectedValue);
        }

        int delta = random.nextInt(100);
        expectedValue += delta;

        // write to hypertable
        SerializedCellsWriter writer = new SerializedCellsWriter(1024, true);
        writer.add("A", "D", "B", SerializedCellsFlag.NULL, ByteBuffer.wrap(Long.toString(delta).getBytes()));
        client.set_cells_serialized(ns, table_name, writer.buffer());
      }
      if (!checkCounter(client, ns, table_name, expectedValue))
        System.out.println(id + ": failure");
      else
        System.out.println(id + ": success");
    } catch (Exception e) {
      e.printStackTrace();
    }
  }
}