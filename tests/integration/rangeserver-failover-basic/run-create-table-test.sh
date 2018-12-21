#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current/"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL="$HT_HOME/bin/ht shell"
SCRIPT_DIR=`dirname $0`
MAX_KEYS=${MAX_KEYS:-"500000"}
RS1_PIDFILE=$HT_HOME/run/RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/RangeServer.rs2.pid
RS3_PIDFILE=$HT_HOME/run/RangeServer.rs3.pid
RUN_DIR=`pwd`

. $HT_HOME/bin/ht-env.sh

. $SCRIPT_DIR/utilities.sh

kill_all_rs
$HT_HOME/bin/ht-stop-servers.sh

# get rid of all old logfiles
\rm -rf $HT_HOME/log/*
\rm /tmp/failover-run9-output
\rm metadata.* dbdump-* rs*dump.* 
\rm -rf fs fs_pre

# generate golden output file
gen_test_data

INDUCER_ARG=
let j=1
[ $TEST == $j ] && INDUCER_ARG="--induce-failure=user-load-range-4:exit:0"
let j+=1
[ $TEST == $j ] && INDUCER_ARG="--induce-failure=add-staged-range-2:exit:0"

# stop and start servers
rm metadata.* keys.* rs*dump.* 
rm -rf fs fs_pre
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --clear --config=${SCRIPT_DIR}/test.cfg

# start the rangeservers
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=15870 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs1.output&
wait_for_server_connect
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 $INDUCER_ARG \
   --Hypertable.RangeServer.Port=15871 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs2.output&
sleep 3
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS3_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs3 $INDUCER_ARG \
   --Hypertable.RangeServer.Port=15872 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs3.output&
sleep 10

# create table
$HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

# verify recovery ocurred
wait_for_recovery

# write data 
$HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
    --max-keys=$MAX_KEYS --row-seed=$ROW_SEED --table=LoadTest
if [ $? != 0 ] ; then
    echo "Problem loading table 'LoadTest', exiting ..."
    exit 1
fi

# dump keys
dump_keys dbdump-a.create-table
if [ $? -ne 0 ] ; then
  kill_all_rs
  $HT_HOME/bin/ht-stop-servers.sh
  exit 1
fi

# stop servers
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 2
kill_rs 3

echo "Test passed"

exit 0
