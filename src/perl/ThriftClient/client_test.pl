#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

#!/usr/bin/env perl

use Hypertable::ThriftClient;
use Data::Dumper;

sub validate_scan_profile_data {
    my $label = shift(@_);
    my $scanner = shift(@_);
    my $profile_data = shift(@_);
    if (scalar @{$profile_data->{servers}} != 1) {
        print "[$label] Expected profile data to have non-empty servers field\n";
        exit 1;
    }
    if ($profile_data->{servers}[0] != "rs1") {
        print "[$label] Expected profile data servers to contain rs1 but got ".
            ($profile_data->{servers}[0])."\n";
        exit 1;
    }
    if ($scanner != 0 && $profile_data->id != $scanner) {
        print "[$label] Expected profile data to have id $scanner but got ".
            ($profile_data->id)."\n";
        exit 1;
    }
    if ($profile_data->bytes_scanned == 0 || $profile_data->bytes_returned == 0) {
        print "[$label] Expected profile data to have non-zero bytes scanned ".
            "and returned but got bytes_scanned=".($profile_data->bytes_scanned).
            " and bytes_returned=".($profile_data->bytes_returned)."\n";
        exit 1;
    }
    if ($profile_data->cells_scanned == 0 || $profile_data->cells_returned == 0) {
        print "[$label] Expected profile data to have non-zero cells scanned ".
            "and returned but got cells_scanned=".($profile_data->cells_scanned).
            " and cells_returned=".($profile_data->cells_returned)."\n";
        exit 1;
    }
    if ($profile_data->subscanners != 1) {
        print "[$label] Expected profile data to have 1 subscanner but got ".
            ($profile_data->subscanners)."\n";
        exit 1;
    }
}


my $client = new Hypertable::ThriftClient("localhost", 15867);

print "HQL examples\n";
my $namespace = $client->namespace_open("test");
print Dumper($client->hql_exec($namespace,"show tables"));
my $result = $client->hql_exec($namespace,"select * from thrift_test max_versions 1");
print Dumper($result);
validate_scan_profile_data("HqlResult", 0, $result->scan_profile_data);

print "mutator examples\n";
my $mutator = $client->mutator_open($namespace, "thrift_test");
my $key = new Hypertable::ThriftGen::Key({row => 'perl-k1',
                                          column_family => 'col'});
my $cell = new Hypertable::ThriftGen::Cell({key => $key,
                                            value => 'perl-v1'});
$client->mutator_set_cell($mutator, $cell);
$client->mutator_flush($mutator);
$client->mutator_close($mutator);

print "shared mutator examples\n";
my $mutate_spec = new Hypertable::ThriftGen::MutateSpec({appname => "test-perl",
                                                         flush_interval => 1000,
                                                         flags => 0});
$key = new Hypertable::ThriftGen::Key({row => 'perl-put-k1',
                                       column_family => 'col'});
$cell = new Hypertable::ThriftGen::Cell({key => $key,
                                         value => 'perl-put-v1'});
$client->shared_mutator_set_cell($namespace, "thrift_test", $mutate_spec, $cell);

$key = new Hypertable::ThriftGen::Key({row => 'perl-put-k2',
                                       column_family => 'col'});
$cell = new Hypertable::ThriftGen::Cell({key => $key,
                                         column_family => 'col',
                                         value => 'perl-put-v2'});
$client->shared_mutator_refresh($namespace, "thrift_test", $mutate_spec);
$client->shared_mutator_set_cell($namespace, "thrift_test", $mutate_spec, $cell);
sleep(2);

print "scanner examples\n";
my $scanner = $client->scanner_open($namespace, "thrift_test",
    new Hypertable::ThriftGen::ScanSpec({versions => 1}));

my $cells = $client->scanner_get_cells($scanner);

while (scalar @$cells) {
  print Dumper($cells);
  $cells = $client->scanner_get_cells($scanner);
}
my $profile_data = $client->scanner_get_profile_data($scanner);
validate_scan_profile_data("scanner", $scanner, $profile_data);
$client->scanner_close($scanner);

print "asynchronous examples\n";
my $future = $client->future_open();
my $color_scanner = $client->async_scanner_open($namespace, "FruitColor", $future,
    new Hypertable::ThriftGen::ScanSpec({versions => 1}));
my $location_scanner = $client->async_scanner_open($namespace, "FruitLocation", $future,
    new Hypertable::ThriftGen::ScanSpec({versions => 1}));
my $energy_scanner = $client->async_scanner_open($namespace, "FruitEnergy", $future,
    new Hypertable::ThriftGen::ScanSpec({versions => 1}));

my $expected_cells = 6;
my $num_cells=0;

while (1) {
  my $result = $client->future_get_result($future);
  print Dumper($result);
  last if ($result->{is_empty}==1 || $result->{is_error}==1 || $result->{is_scan}!=1);
  my $cells = $result->{cells};
  foreach my $cell (@$cells){
    print Dumper($cell);
    $num_cells++;
  }
  if ($num_cells >= 6) {
    $client->future_cancel($future);
    last;
  }
}

# This should not cause problems with referencing scanners
$client->future_close($future);

$profile_data = $client->async_scanner_get_profile_data($color_scanner);
validate_scan_profile_data("color_scanner", $color_scanner, $profile_data);
$client->async_scanner_close($color_scanner);

$profile_data = $client->async_scanner_get_profile_data($location_scanner);
validate_scan_profile_data("location_scanner", $location_scanner, $profile_data);
$client->async_scanner_close($location_scanner);

$profile_data = $client->async_scanner_get_profile_data($energy_scanner);
validate_scan_profile_data("energy_scanner", $energy_scanner, $profile_data);
$client->async_scanner_close($energy_scanner);;

die "Expected $expected_cells cells got $num_cells." if ($num_cells != $expected_cells);

print "regexp scanner example\n";
$scanner = $client->scanner_open($namespace, "thrift_test",
    new Hypertable::ThriftGen::ScanSpec({versions => 1, row_regexp=>"k", value_regexp=>"^v[24]",
    columns=>["col"]}));

my $cells = $client->scanner_get_cells($scanner);

while (scalar @$cells) {
  print Dumper($cells);
  $cells = $client->scanner_get_cells($scanner);
}
$client->scanner_close($scanner);
$client->namespace_close($namespace);
