#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL="$HT_HOME/bin/ht shell"
SCRIPT_DIR=`dirname $0`
MAX_KEYS=${MAX_KEYS:-"500000"}
RS1_PIDFILE=$HT_HOME/run/RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/RangeServer.rs2.pid
RS3_PIDFILE=$HT_HOME/run/RangeServer.rs3.pid
RS4_PIDFILE=$HT_HOME/run/RangeServer.rs4.pid
RS5_PIDFILE=$HT_HOME/run/RangeServer.rs5.pid
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

# start the rangeservers
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=15870 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs1.output&
wait_for_server_connect
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=15871 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs2.output&
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS3_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs3 \
   --Hypertable.RangeServer.Port=15872 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs3.output&
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS4_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs4 \
   --Hypertable.RangeServer.Port=15873 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs4.output&
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS5_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs5 \
   --Hypertable.RangeServer.Port=15874 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs5.output&

# create table
$HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

# write data 
$HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
    --max-keys=$MAX_KEYS --row-seed=$ROW_SEED --table=LoadTest \
    --Hypertable.Mutator.FlushDelay=50
if [ $? != 0 ] ; then
    $HT_HOME/bin/ht-stop-servers.sh
    kill_rs 1
    kill_rs 2
    kill_rs 3
    kill_rs 4
    kill_rs 5
    mkdir failed-run-quorum
    cp rangeserver.rs*.output $HT_HOME/log/* failed-run-quorum
    cp -r $HT_HOME/fs failed-run-quorum
    echo "Problem loading table 'LoadTest', exiting ..."
    exit 1
fi

# kill rs5 and wait for it to be recovered
kill_rs 5
wait_for_recovery rs5


# kill rs4 and wait for it to be recovered
kill_rs 4
wait_for_recovery rs4

# dump keys
dump_keys dbdump.two-serial
if [ $? -ne 0 ] ; then
  kill_all_rs
  $HT_HOME/bin/ht-stop-servers.sh
  exit 1
fi

# stop servers
$HT_HOME/bin/ht-stop-servers.sh
kill_all_rs

echo "Test passed"

exit 0
