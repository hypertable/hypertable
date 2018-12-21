#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL="$HT_HOME/bin/ht shell"
SCRIPT_DIR=`dirname $0`
MAX_KEYS=${MAX_KEYS:-"200000"}
RS1_PIDFILE=$HT_HOME/run/RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/RangeServer.rs2.pid
RUN_DIR=`pwd`

. $HT_HOME/bin/ht-env.sh

. $SCRIPT_DIR/utilities.sh

kill_all_rs
$HT_HOME/bin/ht-stop-servers.sh

# get rid of all old logfiles
\rm -rf $HT_HOME/log/*
rm metadata.* dbdump-.* rs*dump.* 
rm -rf fs fs_pre

# generate golden output file
gen_test_data

# stop and start servers
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --clear --config=${SCRIPT_DIR}/test.cfg

# start both rangeservers
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=15870 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs1.output&
wait_for_server_connect
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=15871 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs2.output&

# start a standby-master
$HT_HOME/bin/ht Master --verbose --pidfile=$HT_HOME/run/Master.pid \
   --port=37011 --config=${SCRIPT_DIR}/test.cfg 2>&1 > $HT_HOME/log/Master.log&

# create table
$HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

# write data 
$HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
    --max-keys=$MAX_KEYS --row-seed=$ROW_SEED --table=LoadTest \
    --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=2M \
    --Hypertable.Mutator.FlushDelay=250
if [ $? != 0 ] ; then
    echo "Problem loading table 'LoadTest', exiting ..."
    exit 1
fi

sleep 2

# kill rs1 and the master
stop_rs 1
kill -9 `cat $HT_HOME/run/Master.pid`
\rm -f $HT_HOME/run/Master.pid

# wait for recovery to complete 
wait_for_recovery rs1

# dump keys
dump_keys dbdump-a.6
if [ $? -ne 0 ] ; then
  kill_all_rs
  $HT_HOME/bin/ht-stop-servers.sh
  exit 1
fi

# bounce servers
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 2
killall htMaster

# start master and rs2
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --config=${SCRIPT_DIR}/test.cfg
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=15871 --config=${SCRIPT_DIR}/test.cfg 2>&1 >> rangeserver.rs2.output&

# dump keys
dump_keys dbdump-b.6
if [ $? -ne 0 ] ; then
  kill_all_rs
  $HT_HOME/bin/ht-stop-servers.sh
  exit 1
fi

# stop servers
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 2

echo "Test passed"

exit 0
