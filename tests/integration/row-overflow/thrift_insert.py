#!/usr/bin/env python

import sys
import time
from hypertable.thrift_client.thriftclient import ThriftClient
from hypertable.thrift_client.hyperthrift.gen.ttypes import *

if (len(sys.argv) < 4):
  print sys.argv[0], "<table> <row-key> <column>"
  sys.exit(1)

try:
  client = ThriftClient("localhost", 15867)

  namespace = client.namespace_open("/")

  mutator = client.mutator_open(namespace, sys.argv[1], 0, 0)

  client.mutator_set_cell(mutator, Cell(Key(sys.argv[2], sys.argv[3], None), "thrift_insert.py"))
  client.mutator_flush(mutator);

  client.close_namespace(namespace)

except ClientException, e:
  print '%s' % (e.message)
  sys.exit(1)

sys.exit(0)
