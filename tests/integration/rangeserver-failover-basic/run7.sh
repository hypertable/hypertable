#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current/"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL="$HT_HOME/bin/ht shell"
SCRIPT_DIR=`dirname $0`
MAX_KEYS=${MAX_KEYS:-"500000"}
RS1_PIDFILE=$HT_HOME/run/RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/RangeServer.rs2.pid
RS3_PIDFILE=$HT_HOME/run/RangeServer.rs3.pid
MASTER_LOG=$HT_HOME/log/Master.log
RUN_DIR=`pwd`

. $HT_HOME/bin/ht-env.sh

. $SCRIPT_DIR/utilities.sh

kill_all_rs
$HT_HOME/bin/ht-stop-servers.sh

# get rid of all old logfiles
\rm -rf $HT_HOME/log/*
\rm metadata.* dbdump-* rs*dump.* 
\rm -rf fs fs_pre

# generate golden output file
gen_test_data

# stop and start servers
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --no-master --clear --config=${SCRIPT_DIR}/test.cfg

# start the master
start_master

# start the rangeservers
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=15870 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs1.output&
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=15871 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs2.output&
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS3_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs3 \
   --Hypertable.RangeServer.Port=15872 --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs3.output&

# create table
$HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

# write data 
$HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
    --max-keys=$MAX_KEYS --row-seed=$ROW_SEED --table=LoadTest \
    --Hypertable.Mutator.FlushDelay=50
if [ $? != 0 ] ; then
    echo "Problem loading table 'LoadTest', exiting ..."
    exit 1
fi

# kill rs1
stop_rs 1

cp $HT_HOME/run/monitoring/mop.dot .

# after 10 seconds: stop the second RangeServer
sleep 10
kill_rs 2

# wait for recovery to complete 
wait_for_recovery rs1
wait_for_recovery rs2

# dump keys
dump_keys dbdump-a.7
if [ $? -ne 0 ] ; then
  kill_all_rs
  $HT_HOME/bin/ht-stop-servers.sh
  exit 1
fi

# stop servers
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 2
kill_rs 3

# The "Barrier for RECOVERY will be up" will be printed at least twice:
#   1. When rs1 is stopped
#   2. When barrier is pushed back by 5 seconds after rs2 is stopped
L=`grep "Barrier for RECOVERY will be up" $MASTER_LOG  | wc -l`
if [ $L -lt 2 ]
then
  echo "Test failed"
  mkdir failed-run-graceperiod
  cp $MASTER_LOG rangeserver.rs?.output dbdump* mop.dot failed-run-graceperiod
  exit 1
fi

echo "Test passed"

exit 0
