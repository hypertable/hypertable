#!/usr/bin/env bash

HYPERTABLE_HOME=/opt/hypertable/doug/current
HBASE_HOME=/usr/lib/hbase
REPORT_DIR=/home/doug/benchmark/reports
VALUE_DATA=/data/1/test/enwiki-sample.txt
TEST_NAME=test2
let DATA_SIZE=5000000000000

#
# Hypertable
#
start_hypertable() {
  if [ "$CONFIG" == "" ] ; then
    cap start
  else
    cap -S config=$CONFIG push_config
    cap -S config=$CONFIG start
  fi
}

stop_hypertable() {
  cap stop_test
  cap stop
}

clean_hypertable() {
    cap stop_test
    cap -S additional_args="--force" stop
    cap cleandb
    hadoop fs -expunge
}

restart_hypertable() {
    cap cleandb
    cap dist
    cap push_config
    cap start
    echo "use '/'; create table perftest ( column );" | $HYPERTABLE_HOME/bin/ht shell --config=$HYPERTABLE_HOME/conf/perftest-hypertable.cfg
}

#
# HBase
#
start_hbase() {
  sudo cap start_hbase
}

stop_hbase() {
  cap stop_test
  sudo cap stop_hbase
}

clean_hbase() {
    sudo cap stop_hbase
    sudo -u hbase hadoop fs -rmr /hbase/*
    sudo cap clean_zookeeper
    hadoop fs -expunge
}

restart_hbase() {
    sudo cap stop_hbase
    sudo -u hbase hadoop fs -rmr /hbase/*
    sudo cap clean_zookeeper
    sudo cap start_hbase
    echo "create 'perftest', { NAME => 'column', VERSIONS => 1000000, COMPRESSION => 'SNAPPY', BLOOMFILTER => 'row' }" | $HBASE_HOME/bin/hbase shell
}
