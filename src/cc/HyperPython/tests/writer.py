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

print ("SerializedCellsWriter Test")

num_cells = 1000000
value_multi = 10
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

# write with SerializedCellsWriter
scw = ht_serialize.SerializedCellsWriter(32896, True)
s_sz = 0
for i in range(0, num_cells):
    i = str(i)
    v = i*value_multi
    scw.add("row"+i, "col", 'qly'+i, 0, v, len(v), 255)
    test_input.append('row'+i+'_'+'col:'+'qly'+i+'_'+v)
    s_sz += len(test_input[-1])
print ('est scw sz: '+str(s_sz))
scw.finalize(0)
client.set_cells_serialized(namespace, "thrift_test", scw.get())

res = client.hql_query(namespace, "select * from thrift_test cell_limit " + str(num_cells))
for cell in res.cells:
    output_test.append(cell.key.row+'_'+cell.key.column_family+':'+cell.key.column_qualifier+'_'+cell.value)

client.namespace_close(namespace)
client.close()

if sorted(test_input) == sorted(output_test):
    print (0)
    exit()

print (sorted(test_input))
print (sorted(output_test))
print (len(test_input))
print (len(output_test))
print (1)
exit()

