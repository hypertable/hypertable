#!/usr/bin/env bash

#. ~/.ssh-agent

export BINDIR=$(cd `dirname "$0"` && pwd)
. $BINDIR/test-config.sh

usage() {
  echo ""
  echo "usage: run-test-read-random.sh [--zipfian] <system> <key-size> <value-size>"
  echo ""
  echo "This script is used to run a performance benchmark.  The <system>"
  echo "argument indicates which system to run the test against and"
  echo "can take a value of 'hypertable' or 'hbase'"
  echo ""
}

ZIPF_ARGS=
if [ "$#" -eq 4 ] ; then
  if [ "$1" == "--zipfian" ]; then
    shift
    ZIPF_ARGS="--zipf --cmf-file=$CMF"
  else
    usage
    exit 1
  fi
elif [ "$#" -ne 3 ] ; then
  usage
  exit 1
fi

SYSTEM=$1
shift
KEY_SIZE=$1
shift
VALUE_SIZE=$1
shift

if [ "$SYSTEM" == "hypertable" ] ; then
    STOP_SYSTEM=stop_hypertable
    START_SYSTEM=start_hypertable
elif [ "$SYSTEM" == "hbase" ] ; then
    STOP_SYSTEM=stop_hbase
    START_SYSTEM=start_hbase
else
    echo "ERROR:  Unrecognized system name '$SYSTEM'"
    exit 1
fi

let CELLSIZE=KEY_SIZE+VALUE_SIZE
let KEYMAX=DATA_SIZE/CELLSIZE
let DISTRANGE=100000000
let KEYCOUNT=100000000


${STOP_SYSTEM}
${START_SYSTEM}
sleep 300

cap -S test_driver=$SYSTEM -S client_multiplier=128 -S test_args="--test-name=$TEST_NAME --output-dir=$REPORT_DIR --random $ZIPF_ARGS --key-max=$KEYMAX --submit-exactly=$KEYCOUNT read $KEY_SIZE $VALUE_SIZE $DATA_SIZE" run_test 
