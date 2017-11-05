import sys
import time
from hypertable.thriftclient import *
from hyperthrift.gen.ttypes import *

def validate_scan_profile_data(label, scanner, profile_data):
  if (len(profile_data.servers) != 1):
    print "["+label+"] Expected profile data to have non-empty servers field\n"
    exit(1)
  if (profile_data.servers[0] != "rs1"):
    print "["+label+"] Expected profile data servers to contain rs1 but got "+ \
        profile_data.servers[0]+"\n"
    exit(1)
  if (scanner != 0 and profile_data.id != scanner):
    print "["+label+"] Expected profile data to have id "+str(scanner)+ \
        ", but got "+ str(profile_data.id)+"\n"
    exit(1)
  if (profile_data.bytes_scanned == 0 or profile_data.bytes_returned == 0):
    print("["+label+"] Expected profile data to have non-zero bytes scanned and"
          " returned but got bytes_scanned="+str(profile_data.bytes_scanned)+ \
          " and bytes_returned="+str(profile_data.bytes_returned)+"\n")
    exit(1)
  if (profile_data.cells_scanned == 0 or profile_data.cells_returned == 0):
    print("["+label+"] Expected profile data to have non-zero cells scanned and"
          " returned but got cells_scanned="+str(profile_data.cells_scanned)+ \
          " and cells_returned="+str(profile_data.cells_returned)+"\n")
    exit(1)
  if (profile_data.subscanners != 1):
    print "["+label+"] Expected profile data to have 1 subscanner but got "+ \
        str(profile_data.subscanners)+"\n"
    exit(1)
    

try:
  client = ThriftClient("localhost", 15867)
  print "HQL examples"

  try:
    namespace = client.namespace_open("bad")
  except:
    print "Caught exception when tyring to open 'bad' namespace"

  namespace = client.namespace_open("test")
  res = client.hql_query(namespace, "show tables")
  print res
  res = client.hql_query(namespace, "select * from thrift_test")
  validate_scan_profile_data("HqlResult", 0, res.scan_profile_data)
  print res

  print "mutator examples";
  mutator = client.mutator_open(namespace, "thrift_test", 0, 0);
  client.mutator_set_cell(mutator, Cell(Key("py-k1", "col", None), "py-v1"))
  client.mutator_flush(mutator);
  client.mutator_close(mutator);

  print "shared mutator examples";
  mutate_spec = MutateSpec("test_py", 1000, 0);
  client.shared_mutator_set_cell(namespace, "thrift_test", mutate_spec, Cell(Key("py-put-k1", "col", None), "py-put-v1"))
  client.shared_mutator_refresh(namespace, "thrift_test", mutate_spec)
  client.shared_mutator_set_cell(namespace, "thrift_test", mutate_spec, Cell(Key("py-put-k2", "col", None), "py-put-v2"))
  time.sleep(2)

  print "scanner examples";
  scanner = client.scanner_open(namespace, "thrift_test",
                                ScanSpec(None, None, None, 1));

  while True:
    cells = client.scanner_get_cells(scanner)
    if (len(cells) == 0):
      break
    print cells

  profile_data = client.scanner_get_profile_data(scanner)
  validate_scan_profile_data("scanner", scanner, profile_data)

  client.scanner_close(scanner)

  print "asynchronous api examples\n";
  future = client.future_open(0);
  mutator_async_1 = client.async_mutator_open(namespace, "thrift_test", future, 0);
  mutator_async_2 = client.async_mutator_open(namespace, "thrift_test", future, 0);
  client.async_mutator_set_cell(mutator_async_1, Cell(Key("py-k1","col", None), "py-v1-async"));
  client.async_mutator_set_cell(mutator_async_2, Cell(Key("py-k1","col", None), "py-v2-async"));
  client.async_mutator_flush(mutator_async_1);
  client.async_mutator_flush(mutator_async_2);

  num_results=0;
  while True:
    result = client.future_get_result(future, 0);
    if(result.is_empty):
      break
    num_results+=1;
    print result;
    if (result.is_error or result.is_scan):
      print "Unexpected result\n"
      exit(1);
    if (num_results>2):
      print "Expected only 2 results\n"
      exit(1)

  if (num_results!=2):
    print "Expected only 2 results\n"
    exit(1)


  if (client.future_is_cancelled(future) or client.future_is_full(future) or not (client.future_is_empty(future)) or client.future_has_outstanding(future)):
    print "Future object in unexpected state"
    exit(1)

  client.async_mutator_close(mutator_async_1)
  client.async_mutator_close(mutator_async_2)

  color_scanner = client.async_scanner_open(namespace, "FruitColor", future, ScanSpec(None, None, None, 1));
  location_scanner = client.async_scanner_open(namespace, "FruitLocation", future, ScanSpec(None, None, None, 1));
  energy_scanner = client.async_scanner_open(namespace, "FruitEnergy", future, ScanSpec(None, None, None, 1));

  expected_cells = 6;
  num_cells = 0;

  while True:
    result = client.future_get_result(future, 0);
    print result;
    if (result.is_empty or result.is_error or not(result.is_scan) ):
      print "Unexpected result\n"
      exit(1);
    for cell in result.cells:
      print cell;
      num_cells+=1;
    if(num_cells >= 6):
      client.future_cancel(future);
      break;

  if (not client.future_is_cancelled(future)):
    print "Expected future ops to be cancelled\n"
    exit(1)

  print "regexp scanner example";
  scanner = client.scanner_open(namespace, "thrift_test",
      ScanSpec(None, None, None, 1, 0, None, None, ["col"], False,0, 0, "k", "v[24]"));

  while True:
    cells = client.scanner_get_cells(scanner)
    if (len(cells) == 0):
      break
    print cells

  # This should not cause scanners or mutators to crash on close
  client.future_close(future)

  client.scanner_close(scanner)

  profile_data = client.async_scanner_get_profile_data(color_scanner)
  validate_scan_profile_data("color_scanner", color_scanner, profile_data)
  client.async_scanner_close(color_scanner);

  profile_data = client.async_scanner_get_profile_data(location_scanner)
  validate_scan_profile_data("location_scanner", location_scanner, profile_data)
  client.async_scanner_close(location_scanner);

  profile_data = client.async_scanner_get_profile_data(energy_scanner)
  validate_scan_profile_data("energy_scanner", energy_scanner, profile_data)
  client.async_scanner_close(energy_scanner);

  client.namespace_close(namespace)
except:
  print sys.exc_info()
  raise
