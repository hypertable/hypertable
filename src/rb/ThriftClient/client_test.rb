#!/usr/bin/env ruby
require 'rubygems'
require File.dirname(__FILE__) + '/hypertable/thrift_client'
require 'pp'
include Hypertable::ThriftGen;

def validate_scan_profile_data(label, scanner, profile_data)
  if profile_data.servers.length != 1
    puts "[%s] Expected profile data to have non-empty servers field" % [label]
    exit 1
  end
  if profile_data.servers[0] != "rs1"
    puts "[%s] Expected profile data servers to contain rs1 but got %s" %
      [label, profile_data.servers[0]]
    exit 1
  end
  if scanner != 0 && profile_data.id != scanner
    puts "[%s] Expected profile data to have id %d but got %d" %
      [label, scanner, profile_data.id]
    exit 1
  end
  if profile_data.bytes_scanned == 0 || profile_data.bytes_returned == 0:
    puts "[%s] Expected profile data to have non-zero bytes scanned and "\
      "returned but got bytes_scanned=%d and bytes_returned=%d" %
      [label, profile_data.bytes_scanned, profile_data.bytes_returned]
    exit 1
  end
  if profile_data.cells_scanned == 0 || profile_data.cells_returned == 0:
    puts "[%s] Expected profile data to have non-zero cells scanned and "\
      "returned but got cells_scanned=%d and cells_returned=%d" %
      [label, profile_data.cells_scanned, profile_data.cells_returned]
    exit 1
  end
  if profile_data.subscanners != 1
    puts "[%s] Expected profile data to have 1 subscanner but got %d" %
      [label, profile_data.subscanners]
    exit 1
  end
end


begin
  Hypertable.with_thrift_client("127.0.0.1", 15867) do |client|
    puts "testing hql queries..."
    ns = client.namespace_open("test");
    res = client.hql_query(ns, "drop table if exists thrift_test");
    res = client.hql_query(ns, "create table thrift_test ( col )");
    res = client.hql_query(ns, "insert into thrift_test values \
                           ('2008-11-11 11:11:11', 'k1', 'col', 'v1'), \
                           ('2008-11-11 11:11:11', 'k2', 'col', 'v2'), \
                           ('2008-11-11 11:11:11', 'k3', 'col', 'v3')");
    res = client.hql_query(ns, "select * from thrift_test");
    validate_scan_profile_data("HqlResult", 0, res.scan_profile_data)
    pp res

    puts "testing scanner api..."
    # testing scanner api
    client.with_scanner(ns, "thrift_test", ScanSpec.new()) do |scanner|
      client.each_cell(scanner) { |cell| pp cell }
      profile_data = client.scanner_get_profile_data(scanner)
      validate_scan_profile_data("scanner", scanner, profile_data)
    end
    
    puts "testing asynchronous api..."
    # testing asynchronous scanner api
    client.with_future() do |future|
      mutator_async_1 = client.async_mutator_open(ns, "thrift_test", future, 0)
      mutator_async_2 = client.async_mutator_open(ns, "thrift_test", future, 0)
      key = Key.new 
      key.row = "k1"
      key.column_family = "col"
      cell = Cell.new
      cell.key = key
      cell.value = "v1-async-rb"
      client.async_mutator_set_cell(mutator_async_1, cell)
      key = Key.new 
      key.row = "k2"
      key.column_family = "col"
      cell = Cell.new
      cell.key = key
      cell.value = "v2-async-rb"
      client.async_mutator_set_cell(mutator_async_2, cell)
      client.async_mutator_flush(mutator_async_1)
      client.async_mutator_flush(mutator_async_2)
      num_results=0
      while (true)
        result = client.future_get_result(future, 0)
        if result.is_empty || result.is_error || result.is_scan
          break
        end
        num_results = num_results+1
      end
      
      if (num_results != 2)
        puts "Expected 2 results from flushes got {#num_results}"
        exit 1
      end 
      client.async_mutator_close(mutator_async_1)
      client.async_mutator_close(mutator_async_2)
      
      if (client.future_is_cancelled(future) || client.future_is_full(future) || client.future_has_outstanding(future) || !client.future_is_empty(future))
        puts "Future operations in unexpected state"
        exit 1
      end

      color_scanner = client.async_scanner_open(ns, "FruitColor", future, ScanSpec.new())
      location_scanner = client.async_scanner_open(ns, "FruitLocation", future, ScanSpec.new())
      energy_scanner = client.async_scanner_open(ns, "FruitEnergy", future, ScanSpec.new())
      expected_cells=6
      num_cells=0
      while (true)
        result = client.future_get_result(future, 0)
        if result.is_empty || result.is_error || !result.is_scan
          break
        end
        result.cells.each do |cell|
          pp cell
          num_cells = num_cells+1
        end
        if (num_cells >= 6)
          client.future_cancel(future)
          break
        end
      end

      profile_data = client.async_scanner_get_profile_data(color_scanner)
      validate_scan_profile_data("scanner", color_scanner, profile_data)
      client.async_scanner_close(color_scanner)

      profile_data = client.async_scanner_get_profile_data(location_scanner)
      validate_scan_profile_data("scanner", location_scanner, profile_data)
      client.async_scanner_close(location_scanner)

      profile_data = client.async_scanner_get_profile_data(energy_scanner)
      validate_scan_profile_data("scanner", energy_scanner, profile_data)
      client.async_scanner_close(energy_scanner)

      if (num_cells != expected_cells || !client.future_is_cancelled(future))
        puts "Expected #{expected_cells} got {#num_cells} and future to be cancelled"
        exit 1
      end
    end

    puts "testing mutator api..."
    client.with_mutator(ns, "thrift_test") do |mutator|
      key = Key.new
      key.row = "k4"
      key.column_family = "col"
      key.timestamp = 1226401871000000000; # 2008-11-11 11:11:11
      cell = Cell.new
      cell.key = key
      cell.value = "v4"
      client.mutator_set_cell(mutator, cell);
    end
    
    puts "testing shared mutator api..."
    
    mutate_spec = MutateSpec.new
    mutate_spec.appname = "test-ruby"
    mutate_spec.flush_interval = 1000
    mutate_spec.flags = 0

    key = Key.new
    key.row = "ruby-put-k1"
    key.column_family = "col"
    key.timestamp = 1226401871000000000; # 2008-11-11 11:11:11
    cell = Cell.new
    cell.key = key
    cell.value = "ruby-put-v1"
    client.shared_mutator_set_cell(ns, "thrift_test", mutate_spec, cell);

    key = Key.new
    key.row = "ruby-put-k2"
    key.column_family = "col"
    key.timestamp = 1226401871000000000; # 2008-11-11 11:11:11
    cell = Cell.new
    cell.key = key
    cell.value = "ruby-put-v2"
    client.shared_mutator_refresh(ns, "thrift_test", mutate_spec);
    client.shared_mutator_set_cell(ns, "thrift_test", mutate_spec, cell);
   
    client.with_mutator(ns, "thrift_test") do |mutator|
      key = Key.new
      key.row = "k4"
      key.column_family = "col"
      key.timestamp = 1226401871000000000; # 2008-11-11 11:11:11
      cell = Cell.new
      cell.key = key
      cell.value = "v4"
      client.mutator_set_cell(mutator, cell);
    end
    
    puts "checking for k4..."
    pp client.hql_query(ns, "select * from thrift_test where row > 'k3'")
    client.namespace_close(ns)
  end
rescue
  pp $!
  exit 1
end
