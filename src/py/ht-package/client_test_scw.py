# -- coding: utf-8 --

from hypertable.thrift_client.thriftclient import ThriftClient
from hypertable.thrift_client.serialized_cells import Writer

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
scw = Writer(32896, True)
s_sz = 0
for i in range(0, num_cells):
    i = str(i)
    v = i*value_multi
    scw.add("row"+i, "col", 'qly'+i, 0, v, len(v), 255)
    test_input.append(('row'+i+'_'+'col:'+'qly'+i+'_'+v))
    s_sz += len(test_input[-1])
print ('est scw sz: '+str(s_sz))
scw.finalize(0)
print ('buf len: '+str(len(scw)))
client.set_cells_serialized(namespace, "thrift_test", scw.get())

res = client.hql_query(namespace, "select * from thrift_test cell_limit " + str(num_cells))
for cell in res.cells:
    # print ([
    #    cell.key.row,
    #    cell.key.column_family,
    #    cell.key.column_qualifier,
    #    cell.value.decode()
    # ])
    output_test.append(cell.key.row+'_'+cell.key.column_family+':'+cell.key.column_qualifier+'_'+cell.value.decode())

client.namespace_close(namespace)
client.close()
if sorted(test_input) != sorted(output_test):
    print (sorted(test_input)[0])
    print (sorted(output_test)[0])
    print (len(test_input))
    print (len(output_test))
    exit(1)
