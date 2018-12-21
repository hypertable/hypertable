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
\rm /tmp/failover-run9-output
\rm metadata.* dbdump-* rs*dump.* 
\rm -rf fs fs_pre

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

# now restart the cluster, but do not start rs2
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 1 2
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --Hypertable.Failover.GracePeriod=20000 \
    --config=${SCRIPT_DIR}/test.cfg
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=15870 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs1.output&

# wait for recovery to complete 
wait_for_recovery rs2

# dump keys
dump_keys dbdump-a.10

# bounce servers
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 2

echo "Test passed"
exit 0
