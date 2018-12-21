#!/usr/bin/perl -w

use strict;
$|=1;

my $HYPERTABLE_BASE_INSTALL_DIR="/opt/hypertable/sanjit/";
my $HYPERTABLE_CURRENT_INSTALL_DIR= $HYPERTABLE_BASE_INSTALL_DIR."current/";
my $HYPERTABLE_HOME = $HYPERTABLE_CURRENT_INSTALL_DIR;
my $HYPERTABLE_BIN_DIR=$HYPERTABLE_CURRENT_INSTALL_DIR."bin/";
my $HYPERTABLE_SHELL=$HYPERTABLE_BIN_DIR."/ht hypertable";
my $HYPERTABLE_CONF_FILE = $HYPERTABLE_BASE_INSTALL_DIR."distributed_hyperspace/distributed_hyperspace.cfg";

my $DIST_COMMAND = "cap dist";
my $CLEANDB_COMMAND = "cap cleandb";
my $STARTDB_COMMAND = "cap start";
my $STOPDB_COMMAND = "cap stop";

my $CREATE_TABLE_FILE = $HYPERTABLE_BASE_INSTALL_DIR."distributed_hyperspace/create.hql";
my $SPEC_FILE =  $HYPERTABLE_BASE_INSTALL_DIR."distributed_hyperspace/write.cfg";
my $command;

chdir $HYPERTABLE_BASE_INSTALL_DIR;
&run_cap_command($DIST_COMMAND);
&run_cap_command($CLEANDB_COMMAND);
&run_cap_command($STARTDB_COMMAND);
&run_hql($CREATE_TABLE_FILE);
$command = "$HYPERTABLE_BIN_DIR/ht ht_load_generator update --config $HYPERTABLE_CONF_FILE --spec-file $SPEC_FILE ";
system($command);
$command = "$HYPERTABLE_BIN_DIR/ht hyperspace --config $HYPERTABLE_CONF_FILE --exec 'locate master;'";
my $master = `$command`;
chomp($master);
print $master;



sub run_hql()
{
  my $hql_file = $_[0];
  my $command = "$HYPERTABLE_SHELL --batch --config=$HYPERTABLE_CONF_FILE < $hql_file";
  print "******** Start executing $command ********\n";
  system($command);
  print "******** End executing $command ********\n";
}

sub run_cap_command()
{
  my $command = $_[0];
  print "******** Start executing $command ********\n";
  my $dir = `pwd`;chomp($dir);
  chdir $HYPERTABLE_BASE_INSTALL_DIR;
  system($command);
  chdir $dir;
  print "******** End executing $command ********\n";
}

