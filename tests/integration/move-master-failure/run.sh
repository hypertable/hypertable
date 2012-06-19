#!/bin/bash -x

HT_HOME=${INSTALL_DIR:-"$HOME/hypertable/current"}
SCRIPT_DIR=`dirname $0`
NUM_POLLS=${NUM_POLLS:-"10"}
WRITE_SIZE=${WRITE_SIZE:-"40000000"}
RS1_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs2.pid
RET=0

$HT_HOME/bin/start-test-servers.sh --clear --no-rangeserver --Hypertable.Master.Split.SoftLimitEnabled=false

$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=38060 \
   --Hypertable.RangeServer.Maintenance.Interval 100 \
   --Hypertable.RangeServer.Range.SplitSize=400K 2>&1 > rangeserver.rs1.output&

$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=38061 \
   --Hypertable.RangeServer.Maintenance.Interval 100 \
   --Hypertable.RangeServer.Range.SplitSize=400K 2>&1 > rangeserver.rs2.output&

$HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

$HT_HOME/bin/ht ht_load_generator update \
    --Hypertable.Mutator.FlushDelay=50 \
    --rowkey.component.0.order=random \
    --rowkey.component.0.type=integer \
    --rowkey.component.0.format="%010lld" \
    --rowkey.component.0.min=0 \
    --rowkey.component.0.max=1000000 \
    --row-seed=1 \
    --Field.value.size=1000 \
    --max-bytes=$WRITE_SIZE

echo "use sys; select * from METADATA MAX_VERSIONS 1;" | $HT_HOME/bin/ht shell --batch > metadata.b
while [ $? != 0 ]; do
  sleep 7
  cp metadata.b metadata.a
  echo "use sys; select * from METADATA MAX_VERSIONS 1;" | $HT_HOME/bin/ht shell --batch > metadata.b
  diff metadata.a metadata.b > /dev/null
done

echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38061
echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38060
sleep 1
kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`

$HT_HOME/bin/start-test-servers.sh --no-rangeserver \
  --induce-failure="relinquish-acknowledge-INITIAL-a:pause(3000):0;relinquish-acknowledge-INITIAL-b:exit:0"

$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=38062 2>&1 >> rangeserver.rs1.output&

$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=38063 2>&1 >> rangeserver.rs2.output&

# Wait for RangeServers to come up
echo "use '/'; select * from LoadTest KEYS_ONLY;" | $HT_HOME/bin/ht shell --batch > /dev/null

# Move a range from one RangeServer to the other
HQL_COMMAND=`$SCRIPT_DIR/generate_range_move.py 4`
echo $HQL_COMMAND
echo $HQL_COMMAND | $HT_HOME/bin/ht shell --batch --Hypertable.Request.Timeout=10000

sleep 5

$HT_HOME/bin/ht serverup master
if [ $? == 0 ]; then
  echo "Master did not die as it should have.  Exitting..."
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`
  $HT_HOME/bin/stop-servers.sh
  exit 1
fi

# Restart master
$HT_HOME/bin/start-master.sh

sleep 2

# Move a range from one RangeServer to the other
HQL_COMMAND_REVERSE=`echo $HQL_COMMAND |  sed 's/rs1/rsTMP/g' | sed 's/rs2/rs1/g' | sed 's/rsTMP/rs2/g'`
echo $HQL_COMMAND_REVERSE
echo $HQL_COMMAND_REVERSE | $HT_HOME/bin/ht shell --batch

# Load more data
$HT_HOME/bin/ht ht_load_generator update \
    --Hypertable.Mutator.FlushDelay=20 \
    --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=50000 \
    --rowkey.component.0.order=random \
    --rowkey.component.0.type=integer \
    --rowkey.component.0.format="%010lld" \
    --rowkey.component.0.min=0 \
    --rowkey.component.0.max=1000000 \
    --row-seed=2 \
    --Field.value.size=1000 \
    --max-bytes=$WRITE_SIZE
RET=$?

kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`
$HT_HOME/bin/stop-servers.sh

exit $RET
