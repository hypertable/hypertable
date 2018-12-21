#!/usr/bin/env python

import random
import sys
import time
from hypertable.thrift_client.thriftclient import ThriftClient
from hypertable.thrift_client.hyperthrift.gen.ttypes import *

if (len(sys.argv) < 3):
  print sys.argv[0], "<max-keys>", "<limit>"
  sys.exit(1);

max_keys=int(sys.argv[1])
limit=int(sys.argv[2])

try:
  client = ThriftClient("localhost", 15867)

  ns = client.open_namespace("/")

  end_row = "%05d~" % (max_keys)

  for n in range(1, max_keys):

      start_row = "%05d" % (n)
      row_intervals = [ RowInterval(start_row, True, end_row, True) ];
      ss = ScanSpec(row_intervals = row_intervals, row_limit = limit);

      cells = client.get_cells(ns, "LoadTest", ss);

      if len(cells) != limit:
        print "Limit query starting at '%s' returned %d rows (expected %d)" % (start_row, len(cells), limit)
        sys.exit(1);

  client.close_namespace(ns)

except ClientException, e:
  print '%s' % (e.message)
