#!/usr/bin/env bash

. ~/.ssh-agent

HADOOP_HOME=/opt/hadoop/current
HYPERTABLE_HOME=/opt/hypertable/current
CONFIG=$HYPERTABLE_HOME/conf/hypertable.cfg

HBASE_HOME=/opt/hbase/current
REPORT_DIR=/home/doug/benchmark

TEST_NAME=test1

WRITE_MULTIPLIER="-S client_multiplier=6"
READ_MULTIPLIER="-S client_multiplier=8"
SCAN_MULTIPLIER="-S client_multiplier=6"

let DATA_SIZE=10000000000

usage() {
  echo ""
  echo "usage: run-test-scan.sh <system>"
  echo ""
  echo "This script is used to run a performance benchmark.  The <system>"
  echo "argument indicates which system to run the test against and"
  echo "can take a value of 'hypertable' or 'hbase'"
  echo ""
}

clean_hypertable() {
    cap stop_test
    cap -S additional_args="--force" stop
    cap cleandb
    echo "Pausing for 60 seconds ..."
    sleep 60
}

restart_hypertable() {
    clean_hypertable
    cap start
    echo "use '/'; create table perftest ( column ) COMPRESSOR=\"none\";" | $HYPERTABLE_HOME/bin/ht shell --config=$CONFIG
}

clean_hbase() {
    $HBASE_HOME/bin/stop-hbase.sh
    $HADOOP_HOME/bin/hadoop fs -rmr /hbase
    echo "Pausing for 60 seconds ..."
    sleep 60
}

restart_hbase() {
    clean_hbase
    $HBASE_HOME/bin/start-hbase.sh
    echo "create 'perftest', {NAME => 'column'}" | $HBASE_HOME/bin/hbase shell
}

if [ "$#" -eq 0 ]; then
  usage
  exit 1
fi

SYSTEM=$1
shift

RSTART_SYSTEM=
CLEAN_SYSTEM=

if [ "$SYSTEM" == "hypertable" ] ; then
    RESTART_SYSTEM=restart_hypertable
    CLEAN_SYSTEM=clean_hypertable
elif [ "$SYSTEM" == "hbase" ] ; then
    RESTART_SYSTEM=restart_hbase
    CLEAN_SYSTEM=clean_hbase
else
    echo "ERROR:  Unrecognized system name '$SYSTEM'"
    exit 1
fi

let maxKeys=DATA_SIZE/100
let vsize=10000
while (($vsize >= 10)) ; do
    let keycount=DATA_SIZE/vsize

    # Random Write
    ${RESTART_SYSTEM}
    cap -S test_driver=$SYSTEM $WRITE_MULTIPLIER -S test_args="--test-name=$TEST_NAME --output-dir=$REPORT_DIR --random write $keycount $vsize" run_test 

    echo "Pausing for 60 seconds ..."
    sleep 60

    # Sequential read
    cap -S test_driver=$SYSTEM $READ_MULTIPLIER -S test_args="--submit-at-most=$maxKeys --test-name=$TEST_NAME --output-dir=$REPORT_DIR read $keycount $vsize" run_test

    let vsize=vsize/10
done

${CLEAN_SYSTEM}

