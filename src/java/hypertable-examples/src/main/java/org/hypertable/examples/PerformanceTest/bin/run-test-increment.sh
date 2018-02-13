#!/usr/bin/env bash

. ~/.ssh-agent

HADOOP_HOME=/opt/hadoop/current
HYPERTABLE_HOME=/opt/hypertable/current
HBASE_HOME=/opt/hbase/current
REPORT_DIR=/home/doug/benchmark
TEST_NAME=test1
let DATA_SIZE=80000000000
WRITE_MULTIPLIER="-S client_multiplier=6"

usage() {
  echo ""
  echo "usage: run-test-increment.sh <system>"
  echo ""
  echo "This script is used to run the increment performance benchmark."
  echo "The <system> argument indicates which system to run the test"
  echo "against and can take a value of 'hypertable' or 'hbase'"
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
    echo "use \"/\"; create table perftest ( column COUNTER );" | $HYPERTABLE_HOME/bin/ht shell --config=$HYPERTABLE_HOME/conf/perftest-hypertable.cfg
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

# Random Increment
${RESTART_SYSTEM}
cap -S test_driver=$SYSTEM $WRITE_MULTIPLIER -S test_args="--test-name=$TEST_NAME --output-dir=$REPORT_DIR --key-size=20 --key-max=10000000000 --random incr 10000000000" run_test

