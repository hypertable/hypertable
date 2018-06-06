#!/usr/bin/env python

import sys
import time
from hypertable.thrift_client.thriftclient import ThriftClient
from hypertable.thrift_client.hyperthrift.gen.ttypes import *

if len(sys.argv) < 2:
    print (sys.argv[0], "<hql>")
    sys.exit(1)

try:
    client = ThriftClient("localhost", 15867)

    namespace = client.open_namespace("/")

    res = client.hql_query(namespace, sys.argv[1])

    print ([res.results, res.cells, res.scanner, res.mutator])

    client.close_namespace(namespace)

except ClientException, e:
    print ('%s' % e.message)
