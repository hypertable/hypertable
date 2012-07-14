#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"$HOME/hypertable/current"}
SCRIPT_DIR=`dirname $0`
NUM_POLLS=${NUM_POLLS:-"10"}
WRITE_SIZE=${WRITE_SIZE:-"40000000"}
RS1_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs2.pid

$HT_HOME/bin/start-test-servers.sh --clear --no-rangeserver

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

sleep 3

echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38061
echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38060

sleep 1

kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`

$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=38062 2>&1 >> rangeserver.rs1.output&

$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=38063 2>&1 >> rangeserver.rs2.output&

sleep 10

echo "About to load data ... " + `date`

#
# Load Test
#

$HT_HOME/bin/ht ht_load_generator update \
    --Hypertable.Mutator.FlushDelay=20 \
    --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=50000 \
    --rowkey.component.0.order=random \
    --rowkey.component.0.type=integer \
    --rowkey.component.0.format="%010lld" \
    --rowkey.component.0.min=0 \
    --rowkey.component.0.max=1000000 \
    --row-seed=2 \
    --Field.value.size=5000 \
    --max-bytes=100000000 2>&1 > load.output&

LOAD_PID=${!}

for ((i=0; i<30; i++)) ; do
  HQL_COMMAND=`$SCRIPT_DIR/generate_range_move.py 4`
  echo "Issuing HQL: $HQL_COMMAND"
  echo $HQL_COMMAND | $HT_HOME/bin/ht shell --batch
done

wait $LOAD_PID

echo "use '/'; select * from LoadTest KEYS_ONLY;" | $HT_HOME/bin/ht shell --batch > dump.output

if [ ! -e dump.golden ] ; then
  cp $SCRIPT_DIR/dump.golden.gz .
  gzip -d dump.golden.gz
fi

diff dump.output dump.golden
if [ $? != 0 ] ; then
  #exec 1>&-
  #sleep 86400
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`
  $HT_HOME/bin/clean-database.sh
  exit 1
fi

$HT_HOME/bin/ht ht_load_generator query \
    --rowkey.component.0.order=random \
    --rowkey.component.0.type=integer \
    --rowkey.component.0.format="%010lld" \
    --rowkey.component.0.min=0 \
    --rowkey.component.0.max=1000000 \
    --row-seed=3 \
    --Field.value.size=1000 \
    --max-keys=50000 2>&1 > query.output&

QUERY_PID=${!}

for ((i=0; i<15; i++)) ; do
  HQL_COMMAND=`$SCRIPT_DIR/generate_range_move.py 4`
  echo "Issuing HQL: $HQL_COMMAND"
  echo $HQL_COMMAND | $HT_HOME/bin/ht shell --batch
done

wait $QUERY_PID

fgrep ERROR query.output > errors.txt
if [ -s errors.txt ] ; then
  cat errors.txt
  #exec 1>&-
  #sleep 86400
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`
  $HT_HOME/bin/clean-database.sh
  exit 1
fi

let iteration=0
$HT_HOME/bin/ht metalog_dump /hypertable/servers/rs1/log/rsml | fgrep "load_acknowledged=false" > errors.txt
$HT_HOME/bin/ht metalog_dump /hypertable/servers/rs2/log/rsml | fgrep "load_acknowledged=false" >> errors.txt
while [ -s errors.txt ] && [ $iteration -lt 5 ]; do
  sleep 10
  $HT_HOME/bin/ht metalog_dump /hypertable/servers/rs1/log/rsml | fgrep "load_acknowledged=false" > errors.txt
  $HT_HOME/bin/ht metalog_dump /hypertable/servers/rs2/log/rsml | fgrep "load_acknowledged=false" >> errors.txt
  let iteration=iteration+1
done

if [ -s errors.txt ] ; then
  cat errors.txt
  #exec 1>&-
  #sleep 86400
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`
  $HT_HOME/bin/clean-database.sh
  exit 1
fi

kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`
$HT_HOME/bin/clean-database.sh

exit 0


