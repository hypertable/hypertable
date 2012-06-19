#!/usr/bin/env python

import random
import sys
import time
from hypertable.thriftclient import *
from hyperthrift.gen.ttypes import *

if (len(sys.argv) < 2):
  print sys.argv[0], "<duration-seconds>"
  sys.exit(1);

duration=int(sys.argv[1])

try:
  client = ThriftClient("localhost", 38080)

  namespace = client.open_namespace("/sys")

  scanner = client.open_scanner(namespace, "METADATA",
                                ScanSpec(None, None, None, 1, 0, None, None, ["StartRow","Location"]));

  ranges = [ ]
  cur = { }

  while True:
    cells = client.next_cells(scanner)
    if (len(cells) == 0):
      break
    for cell in cells:
      colon = cell.key.row.find(":")
      if not 'TableId' in cur or not 'EndRow' in cur:
        cur = { }
        cur['TableId'] = cell.key.row[:colon]
        cur['EndRow'] = cell.key.row[colon+1:]
      elif cell.key.row[:colon] != cur['TableId'] or cell.key.row[colon+1:] != cur['EndRow']:
        ranges.append(cur)
        cur = { }
        cur['TableId'] = cell.key.row[:colon]
        cur['EndRow'] = cell.key.row[colon+1:]
      if cell.key.column_family == "StartRow":
        cur['StartRow'] = cell.value
      elif cell.key.column_family == "Location":
        cur['Location'] = cell.value
      else:
        print "Unrecognized column family'%s'" % (cell.key.column_family)
        sys.exit(1)           

  if 'StartRow' in cur:
    ranges.append(cur)

  client.close_namespace(namespace)

  offset = random.randint(0, len(ranges)-1)

  if ranges[offset]['Location'] == "rs1":
    destination = "rs2"
  elif ranges[offset]['Location'] == "rs2":
    destination = "rs1"
  else:
    print "Unexpected destination: %s" % (ranges[offset]['Location'])
    sys.exit(1)           

  if ranges[offset]['StartRow'] is None:
    print 'balance (\"%s\"[..\"%s\"], \"%s\", \"%s\") duration=%d;' % (ranges[offset]['TableId'], ranges[offset]['EndRow'], ranges[offset]['Location'], destination, duration)
  else:
    print 'balance (\"%s\"[\"%s\"..\"%s\"], \"%s\", \"%s\") duration=%d;' % (ranges[offset]['TableId'], ranges[offset]['StartRow'], ranges[offset]['EndRow'], ranges[offset]['Location'], destination, duration)

#  for range in ranges:
#    print range

except ClientException, e:
  print '%s' % (e.message)
