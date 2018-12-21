
from hypertable.hypertable_client import HypertableClient


import psutil
p = psutil.Process()


def print_num_threads():
    print ("*"*80)
    print ("Num Threads: " + str(p.num_threads()))


print_num_threads()

num_ns = 24
num_child_ns = 10

ht_client = HypertableClient("/opt/hypertable/0.9.8.15/",
                             "/opt/hypertable/0.9.8.15/conf/hypertable.cfg",
                             default_timeout_ms=10000)

print_num_threads()
#print ('create_hql_interpreter')
#inter = ht_client.create_hql_interpreter()

print_num_threads()

ht_client.create_namespace("test_py_ht", if_not_exists=True)
ns = ht_client.open_namespace("test_py_ht")

print_num_threads()

for n in range(1, num_ns+1):
    name = "ns_"+str(n)
    print ('creating ns: '+name+" in test_py_ht")
    ht_client.drop_namespace(name, ns, if_exists=True)
    ht_client.create_namespace(name, ns)
    if not ht_client.exists_namespace(name, ns):
        print ("ERROR not exists_namespace '"+name+"'")
        exit(1)

print_num_threads()

for chk in range(0, 3):
    for n in range(1, num_ns+1):
        name = "ns_"+str(n)
        if not ht_client.exists_namespace(name, ns):
            print ("ERROR not exists_namespace '"+name+"'")
            exit(1)

print_num_threads()

for n in range(1, num_ns+1):
    name = "ns_"+str(n)
    ht_client.drop_namespace(name, ns)
    print ('drop ns: '+name+" in test_py_ht")
    if ht_client.exists_namespace(name, ns):
        print ("ERROR namespace '"+name+"' was not dropped ")
        exit(1)

print_num_threads()

print ('drop ns test_py_ht')
ht_client.drop_namespace("test_py_ht")
if ht_client.exists_namespace("test_py_ht"):
    print ("ERROR namespace 'test_py_ht' was not dropped ")
    exit(1)
print_num_threads()
