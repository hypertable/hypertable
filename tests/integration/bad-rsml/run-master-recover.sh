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

# maybe generate test data
if [ ! -s golden_dump.md5 ] ; then
    $HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
        --max-keys=$DATA_SIZE --row-seed=$DATA_SEED \
        --stdout | cut -f1 | tail -n +2 | sort -u > golden_dump.txt
    $DIGEST < golden_dump.txt > golden_dump.md5
fi

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

## Kill rs2
kill `cat $HT_HOME/run/RangeServer.rs2.pid`

## Wait for "Problem reading RSML" to appear in Master log
grep "Problem reading RSML" $HT_HOME/log/Master.log
while [ $? -ne 0 ]; do
    echo "Waiting for \"Problem reading RSML\" to appear in master log ..."
    sleep 5
    grep "Problem reading RSML" $HT_HOME/log/Master.log    
done

## Restart Master
kill `cat $HT_HOME/run/Master.pid`
\rm -f $HT_HOME/run/Master.pid
$HT_HOME/bin/ht-start-master.sh --config=${SCRIPT_DIR}/test.cfg

# dump keys
$HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
    | grep -v "hypertable" | grep -v "Waiting for connection to Hyperspace" > dbdump.output
if [ $? != 0 ] ; then
    echo "Problem dumping table 'failover-test', exiting ..."
    exit 1
fi

$DIGEST < dbdump.output > dbdump.md5
diff golden_dump.md5 dbdump.md5 > out
if [ $? != 0 ] ; then
    echo "Test $TEST FAILED." >> report.txt
    echo "Test $TEST FAILED." >> errors.txt
    cat out >> report.txt
    touch error
    $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
        | grep -v "hypertable" | grep -v "Waiting for connection to Hyperspace" > dbdump.output.again
    exec 1>&-
    sleep 86400
else
    echo "Test $TEST PASSED." >> report.txt
fi

sleep 5

# Shut down range servers
kill -9 `cat $HT_HOME/run/RangeServer.*.pid`
\rm -f $HT_HOME/run/RangeServer.*.pid

# Shut down remaining servers
$HT_HOME/bin/ht-stop-servers.sh

# Verify RecoveredServers entity
$HT_HOME/bin/ht-start-fsbroker.sh local
$HT_HOME/bin/ht metalog_dump /hypertable/servers/master/log/mml | grep RecoveredServers > mml.output
cat mml.output \
    | perl -e 'while (<>) { s/timestamp=.*?201\d,/timestamp=0,/g; print; }' \
    | perl -e 'while (<>) { s/id=\d*,/id=0,/g; print; }' > mml.stripped
diff mml.stripped ${SCRIPT_DIR}/mml.golden
if [ $? != 0 ] ; then
  echo "MML diff failed"
  exit 1
fi

# Make sure system starts up again

# Start servers (minus range server)
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver \
    --no-thriftbroker --config=${SCRIPT_DIR}/test.cfg 

# Start rs1
$HT_HOME/bin/ht RangeServer \
    --config=${SCRIPT_DIR}/test.cfg \
    --verbose \
    --pidfile=$HT_HOME/run/RangeServer.pid \
    --Hypertable.RangeServer.ProxyName=rs1 \
    --Hypertable.RangeServer.Port=15870 > rangeserver.rs1.output.2 &

# dump keys
$HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
    | grep -v "hypertable" | grep -v "Waiting for connection to Hyperspace" > dbdump.output.2
if [ $? != 0 ] ; then
    echo "Problem dumping table 'failover-test', exiting ..."
    exit 1
fi

$DIGEST < dbdump.output.2 > dbdump.md5.2
diff golden_dump.md5 dbdump.md5.2 > out
if [ $? != 0 ] ; then
  echo "Second dump failed"
  exit 1
fi

$HT_HOME/bin/ht-stop-servers.sh

exit 0
