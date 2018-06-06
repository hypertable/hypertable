# -- coding: utf-8 --
import sys

if sys.version_info.major == 3:
    import imp
    # import importlib as imp
else:
    import imp

from hypertable.thrift_client import *
from hypertable.thrift_client.hyperthrift.gen.ttypes import *

# import libHyperPython as ht_serialize
if sys.argv[1] == 'python':
    ht_serialize = imp.load_dynamic('libHyperPython', sys.argv[2])
elif sys.argv[1] == 'python3':
    ht_serialize = imp.load_dynamic('libHyperPython', sys.argv[2])
elif sys.argv[1] == 'pypy':
    ht_serialize = imp.load_dynamic('libHyperPyPy', sys.argv[2])


print ("SerializedCellsReader Test")

num_cells = 100
value_multi = 200
test_input = []
output_test = []

client = ThriftClient("localhost", 15867)
try:
    client.create_namespace("test")
except:
    pass
namespace = client.namespace_open("test")
client.hql_query(namespace, "drop table if exists thrift_test")
client.hql_query(namespace, "create table thrift_test (col)")

for i in range(0, num_cells):
    i = str(i)
    client.hql_query(
        namespace, "insert into thrift_test values ('2012-10-10','row"+i+"','col:qly"+i+"','"+str(i*value_multi)+"')")
    test_input.append(str('row'+i+'_col:qly'+i+'_'+str(i*value_multi)))

# read with SerializedCellsReader
scanner = client.scanner_open(namespace, "thrift_test", ScanSpec(None, None, None, 1))
while True:
    buf = client.scanner_get_cells_serialized(scanner)
    print ('buf len: '+str(len(buf)))
    if len(buf) <= 5:
        break
    scr = ht_serialize.SerializedCellsReader(buf, len(buf))
    try:
        while scr.has_next():
            c = (scr.get_cell())
            print c
            v = [scr.row(), scr.column_family(), scr.column_qualifier(), scr.value_str(), scr.value_len()]
            print (len(v[-2]), v[-1])
            output_test.append(str(v[0])+"_"+str(v[1])+':'+str(v[2])+"_"+str(v[3]))
    except:
        print (sys.exc_info())

client.scanner_close(scanner)
client.namespace_close(namespace)
client.close()

if sorted(test_input) == sorted(output_test):
    print (0)
    exit()

print (sorted(test_input)[0])
print (sorted(output_test)[0])
print (len(test_input))
print (len(output_test))
print (1)
exit()
