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

# clear state
\rm -rf $HT_HOME/log/*
\rm metadata.* dbdump-* rs*dump.* 
\rm -rf fs fs_pre

gen_test_data

# start servers
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

# dump METADATA location
${HT_HOME}/bin/ht shell --config=${SCRIPT_DIR}/test.cfg --no-prompt --exec "use sys; select Location from METADATA MAX_VERSIONS=1 into file '${RUN_DIR}/metadata.pre';"

# dump ranges
echo "dump nokeys '${RUN_DIR}/rs1_dump.pre'; quit;" | $HT_HOME/bin/ht rangeserver localhost:15870
echo "dump nokeys '${RUN_DIR}/rs2_dump.pre'; quit;" | $HT_HOME/bin/ht rangeserver localhost:15871
# copy state
cp -R ${HT_HOME}/fs ${RUN_DIR}/fs_pre

# kill rs1
stop_rs 1

# wait for recovery to complete 
wait_for_recovery rs1

# dump rs2 ranges
echo "dump nokeys '${RUN_DIR}/rs2_dump.post'; quit;" | $HT_HOME/bin/ht rangeserver localhost:15871

dump_keys dbdump-a.1
if [ $? -ne 0 ] ; then
  kill_all_rs
  $HT_HOME/bin/ht-stop-servers.sh
  exit 1
fi

# dump METADATA location
${HT_HOME}/bin/ht shell --config=${SCRIPT_DIR}/test.cfg --no-prompt --exec "use sys; select Files,Location from METADATA MAX_VERSIONS=1 into file '${RUN_DIR}/metadata.post';"

# bounce servers
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 2

# start master and rs2
$HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --config=${SCRIPT_DIR}/test.cfg
$HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=15871 --config=${SCRIPT_DIR}/test.cfg 2>&1 >> rangeserver.rs2.output&

# dump METADATA location
${HT_HOME}/bin/ht shell --config=${SCRIPT_DIR}/test.cfg --no-prompt --exec "use sys; select Files,Location from METADATA MAX_VERSIONS=1 into file '${RUN_DIR}/metadata.post2';"

dump_keys dbdump-b.1
if [ $? -ne 0 ] ; then
  kill_all_rs
  $HT_HOME/bin/ht-stop-servers.sh
  exit 1
fi

# stop servers
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 2

# check output
ORIGINAL_RANGES=`grep Location metadata.pre|wc -l`
RS1_RANGES=`grep "rs1" metadata.post|wc -l`
RS2_RANGES=`grep "rs2" metadata.post|wc -l`
echo "Total keys returned=${TOTAL_KEYS}, ${TOTAL_KEYS2}, expected keys=${EXPECTED_KEYS}, original ranges=${ORIGINAL_RANGES}, rs1_ranges=${RS1_RANGES}, rs2_ranges=${RS2_RANGES}"

if [ "$RS2_RANGES" -lt "$ORIGINAL_RANGES" ] 
then
  echo "Test failed, expected at least ${ORIGINAL_RANGES} on rs2, found ${RS2_RANGES}"
  mkdir failed-run-basic-1
  cp $HT_HOME/log/Master.log rangeserver.rs1.output rangeserver.rs2.output \
      metadata.pre metadata.post failed-run-basic-1
  cp $HT_HOME/fs/local/hypertable/servers/rs1 failed-run-basic-1
  cp $HT_HOME/fs/local/hypertable/servers/rs2 failed-run-basic-1
  cp *dump* failed-run-basic-1
  exit 1
fi

if [ "$RS1_RANGES" -gt "0" ]
then
  echo "Test failed, expected 0 ranges on rs1, found ${RS1_RANGES}"
  exit 1
fi

echo "Test passed"
$HT_HOME/bin/ht-stop-servers.sh
kill_rs 2

exit 0
