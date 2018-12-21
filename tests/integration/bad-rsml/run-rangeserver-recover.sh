#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL="$HT_HOME/bin/ht shell"
SCRIPT_DIR=`dirname $0`
DATA_SEED=42
DATA_SIZE=${DATA_SIZE:-"2000000"}
DIGEST="openssl dgst -md5"
RUN_DIR=`pwd`

. $HT_HOME/bin/ht-env.sh

# get rid of all old logfiles
\rm -rf $HT_HOME/log/*

# shut down range servers
kill -9 `cat $HT_HOME/run/RangeServer.*.pid`
\rm -f $HT_HOME/run/RangeServer.*.pid

# Start servers (minus range server)
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver \
    --no-thriftbroker --clear --induce-failure="bad-rsml:throw:0" \
    --config=${SCRIPT_DIR}/test.cfg 

# Start rs1
$HT_HOME/bin/ht RangeServer \
    --config=${SCRIPT_DIR}/test.cfg \
    --verbose \
    --pidfile=$HT_HOME/run/RangeServer.rs1.pid \
    --Hypertable.RangeServer.ProxyName=rs1 \
    --Hypertable.RangeServer.Port=15870 > rangeserver.rs1.output &

# Start rs2
$HT_HOME/bin/ht RangeServer \
    --config=${SCRIPT_DIR}/test.cfg \
    --verbose \
    --pidfile=$HT_HOME/run/RangeServer.rs2.pid \
    --Hypertable.RangeServer.ProxyName=rs2 \
    --Hypertable.RangeServer.Port=15871 > rangeserver.rs2.output &

# Create test table
$HT_SHELL --batch < $SCRIPT_DIR/create-test-table.hql
if [ $? != 0 ] ; then
    echo "Unable to create table 'BadRsmlTest', exiting ..."
    exit 1
fi

# Load data
$HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
    --max-keys=$DATA_SIZE --row-seed=$DATA_SEED --table=BadRsmlTest \
    --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=100K \
    
if [ $? != 0 ] ; then
    echo "Problem loading table 'BadRsmlTest', exiting ..."
    exit 1
fi

sleep 10

## Kill servers
kill `cat $HT_HOME/run/Master.pid`
kill `cat $HT_HOME/run/RangeServer.rs*.pid`
$HT_HOME/bin/ht-stop-servers.sh

# Start servers (minus range server)
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver \
    --no-thriftbroker  --config=${SCRIPT_DIR}/test.cfg 

# Start rs1
$HT_HOME/bin/ht RangeServer \
    --config=${SCRIPT_DIR}/test.cfg \
    --verbose \
    --pidfile=$HT_HOME/run/RangeServer.rs1.pid \
    --Hypertable.RangeServer.ProxyName=rs1 \
    --induce-failure="bad-rsml:throw:0" \
    --Hypertable.RangeServer.Port=15870 > rangeserver.rs1.output &

# Start rs2
$HT_HOME/bin/ht RangeServer \
    --config=${SCRIPT_DIR}/test.cfg \
    --verbose \
    --pidfile=$HT_HOME/run/RangeServer.rs2.pid \
    --Hypertable.RangeServer.ProxyName=rs2 \
    --Hypertable.RangeServer.Port=15871 > rangeserver.rs2.output &

## Wait for "Problem reading RSML" to appear in Master log
grep "Problem reading RSML" rangeserver.rs1.output
while [ $? -ne 0 ]; do
    echo "Waiting for \"Problem reading RSML\" to appear in master log ..."
    sleep 5
    grep "Problem reading RSML" rangeserver.rs1.output
done


# Shut down range servers
kill -9 `cat $HT_HOME/run/RangeServer.*.pid`
\rm -f $HT_HOME/run/RangeServer.*.pid

# Shut down remaining servers
$HT_HOME/bin/ht-stop-servers.sh

exit 0
