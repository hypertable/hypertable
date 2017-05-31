import sys
import time
import imp

if sys.argv[1]=='python':
    imp.load_dynamic('libHyperPython',sys.argv[2])
    import libHyperPython as htSerialize
elif sys.argv[1]=='pypy':
    imp.load_dynamic('libHyperPyPy',sys.argv[2])
    import libHyperPyPy as htSerialize

from hypertable.thriftclient import *
from hyperthrift.gen.ttypes import *

try:
  client = ThriftClient("localhost", 15867)
  print "SerializedCellsReader example"

  namespace = client.namespace_open("test")
  client.hql_query(namespace, "drop table if exists thrift_test")
  client.hql_query(namespace, "create table thrift_test (col)")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'row0', 'col', 'value0')")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'row1', 'col', 'value1')")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'row2', 'col', 'value2')")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'row3', 'col', 'value3')")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'row4', 'col', 'value4')")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'row5', 'col', 'value5')")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'collapse_row', 'col:a', 'value6')")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'collapse_row', 'col:b', 'value7')")
  client.hql_query(namespace, "insert into thrift_test values " \
          "('2012-10-10', 'collapse_row', 'col:c', 'value8')")

  # read with SerializedCellsReader
  scanner = client.scanner_open(namespace, "thrift_test",    ScanSpec(None, None, None, 1));
  while True:
    buf = client.scanner_get_cells_serialized(scanner)
    if (len(buf) <= 5):
      break
    scr = htSerialize.SerializedCellsReader(buf, len(buf))
    while scr.has_next():
      print scr.row(),
      print scr.column_family(),
      s = ''
      for i in range(scr.value_len()):
        s += scr.value()[i]
      print s

  client.scanner_close(scanner)
  client.namespace_close(namespace)
except:
  print sys.exc_info()
  raise
