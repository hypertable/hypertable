#!/usr/bin/env bash

#. ~/.ssh-agent

export BINDIR=$(cd `dirname "$0"` && pwd)
. $BINDIR/test-config.sh

usage() {
  echo ""
  echo "usage: run-test-scan.sh <system> <key-size> <value-size>"
  echo ""
  echo "This script is used to run a performance benchmark.  The <system>"
  echo "argument indicates which system to run the test against and"
  echo "can take a value of 'hypertable' or 'hbase'"
  echo ""
}

if [ "$#" -ne 3 ]; then
  usage
  exit 1
fi

SYSTEM=$1
shift
KEY_SIZE=$1
shift
VALUE_SIZE=$1
shift

if [ "$SYSTEM" != "hypertable" ] && [ "$SYSTEM" != "hbase" ] ; then
    echo "ERROR:  Unrecognized system name '$SYSTEM'"
    exit 1
fi

#let KV_SIZE=VALUE_SIZE+10
#let keycount=DATA_SIZE/KV_SIZE

#let maxKeys=DATA_SIZE/100
#SUBMIT_AT_MOST="--submit-at-most=$maxKeys"

#cap -S test_driver=$SYSTEM -S client_multiplier=12 -S test_args="--submit-at-most=$maxKeys --test-name=$TEST_NAME --output-dir=$REPORT_DIR scan $keycount $VALUE_SIZE" run_test
cap -S test_driver=$SYSTEM -S client_multiplier=12 -S test_args="--test-name=$TEST_NAME --output-dir=$REPORT_DIR scan $KEY_SIZE $VALUE_SIZE $DATA_SIZE" run_test
