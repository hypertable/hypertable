import sys
import imp

if sys.argv[1]=='python': ht_serialize = imp.load_dynamic('libHyperPython',sys.argv[2]) # import libHyperPython as ht_serialize
elif sys.argv[1]=='pypy': ht_serialize = imp.load_dynamic('libHyperPyPy',sys.argv[2])

from hypertable.thriftclient import *
from hyperthrift.gen.ttypes import *


try:
  client = ThriftClient("localhost", 15867)
  print "SerializedCellsWriter example"

  namespace = client.namespace_open("test")
  client.hql_query(namespace, "drop table if exists thrift_test")
  client.hql_query(namespace, "create table thrift_test (col)")

  # write with SerializedCellsWriter
  scw = ht_serialize.SerializedCellsWriter(100, True)

  scw.add("row0", "col", "", 0, "cell0", 6, 255)
  scw.add("row1", "col", "", 0, "cell1", 6, 255)
  scw.add("row2", "col", "", 0, "cell2", 6, 255)
  scw.add("row3", "col", "", 0, "cell3", 6, 255)
  scw.add("row4", "col", "", 0, "cell4", 6, 255)
  scw.add("row5", "col", "", 0, "cell5", 6, 255)
  scw.add("collapse_row", "col", "a", 0, "cell6", 6, 255)
  scw.add("collapse_row", "col", "b", 0, "cell7", 6, 255)
  scw.add("collapse_row", "col", "c", 0, "cell8", 6, 255)
  scw.finalize(0)
  client.set_cells_serialized(namespace, "thrift_test", scw.get())

  res = client.hql_query(namespace, "select * from thrift_test")
  for cell in res.cells:
      print cell.key.row, cell.key.column_family, cell.value

  client.namespace_close(namespace)
except:
  print sys.exc_info()
  raise
