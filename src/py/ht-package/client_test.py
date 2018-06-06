import sys
import time
from hypertable.thrift_client.thriftclient import ThriftClient
from hypertable.thrift_client.hyperthrift.gen.ttypes import *

try:
    client = ThriftClient('localhost', 15867)
    print ('HQL examples')

    try:
        namespace = client.namespace_open('bad')
    except:
        print ('Caught exception when trying to open "bad" namespace')

    try:
        client.create_namespace('test')
    except:
        pass
    namespace = client.namespace_open('test')
    client.hql_query(namespace, 'drop table if exists thrift_test')
    client.hql_query(namespace, 'create table thrift_test (col)')

    res = client.hql_query(namespace, 'show tables')
    print (res)
    res = client.hql_query(namespace, 'select * from thrift_test')
    print (res)

    print ('mutator examples')
    mutator = client.mutator_open(namespace, str('thrift_test'), 0, 0)
    print ('mutator examples mutator_set_cell')
    client.mutator_set_cell(mutator, Cell(Key('py-k1', 'col', None), b'py-v1'))
    client.mutator_flush(mutator)
    client.mutator_close(mutator)

    print ('shared mutator examples')
    mutate_spec = MutateSpec('test_py', 1000, 0)
    client.shared_mutator_set_cell(namespace, 'thrift_test', mutate_spec,
                                   Cell(Key('py-put-k1', 'col', None), b'py-put-v1'))
    client.shared_mutator_refresh(namespace, 'thrift_test', mutate_spec)
    client.shared_mutator_set_cell(namespace, 'thrift_test', mutate_spec,
                                   Cell(Key('py-put-k2', 'col', None), b'py-put-v2'))
    time.sleep(2)

    print ('scanner examples')
    scanner = client.scanner_open(namespace, 'thrift_test',
                                  ScanSpec(None, None, None, 1))

    while True:
        cells = client.scanner_get_cells(scanner)
        if len(cells) == 0:
            break
        print (cells)

    client.scanner_close(scanner)

    print ('asynchronous api examples\n')
    future = client.future_open(0)
    mutator_async_1 = client.async_mutator_open(namespace, 'thrift_test', future, 0)
    mutator_async_2 = client.async_mutator_open(namespace, 'thrift_test', future, 0)
    client.async_mutator_set_cell(mutator_async_1, Cell(Key('py-k1', 'col', None), b'py-v1-async'))
    client.async_mutator_set_cell(mutator_async_2, Cell(Key('py-k1', 'col', None), b'py-v2-async'))
    client.async_mutator_flush(mutator_async_1)
    client.async_mutator_flush(mutator_async_2)

    num_results = 0
    while True:
        result = client.future_get_result(future, 0)
        if result.is_empty:
            break
        num_results += 1
        print (result)
        if result.is_error or result.is_scan:
            print ('Unexpected result\n')
            exit(1)
        if num_results > 2:
            print ('Expected only 2 results\n')
            exit(1)

    if num_results != 2:
        print ('Expected only 2 results\n')
        exit(1)

    if (client.future_is_cancelled(future) or client.future_is_full(future) or not (
            client.future_is_empty(future)) or client.future_has_outstanding(future)):
        print ('Future object in unexpected state')
        exit(1)

    client.async_mutator_close(mutator_async_1)
    client.async_mutator_close(mutator_async_2)

    color_scanner = client.async_scanner_open(namespace, 'FruitColor', future, ScanSpec(None, None, None, 1))
    location_scanner = client.async_scanner_open(namespace, 'FruitLocation', future, ScanSpec(None, None, None, 1))
    energy_scanner = client.async_scanner_open(namespace, 'FruitEnergy', future, ScanSpec(None, None, None, 1))

    expected_cells = 6
    num_cells = 0

    while True:
        result = client.future_get_result(future, 0)
        print (result)
        if result.is_empty or result.is_error or not result.is_scan:
            print ('Unexpected result\n')
            exit(1)
        for cell in result.cells:
            print (cell)
            num_cells += 1
        if num_cells >= 6:
            client.future_cancel(future)
            break

    if not client.future_is_cancelled(future):
        print ('Expected future ops to be cancelled\n')
        exit(1)

    print ('regexp scanner example')
    scanner = client.scanner_open(namespace, 'thrift_test',
                                  ScanSpec(None, None, None, 1, 0, None, None, ['col'], False, 0, 0, 'k', 'v[24]'))

    while True:
        cells = client.scanner_get_cells(scanner)
        if len(cells) == 0:
            break
        print (cells)

    # This should not cause scanners or mutators to crash on close
    client.future_close(future)

    client.scanner_close(scanner)
    client.async_scanner_close(color_scanner)
    client.async_scanner_close(location_scanner)
    client.async_scanner_close(energy_scanner)

    client.namespace_close(namespace)
except:
    print (sys.exc_info())
    raise
