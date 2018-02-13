#!/usr/bin/env bash

#. ~/.ssh-agent

export BINDIR=$(cd `dirname "$0"` && pwd)
. $BINDIR/test-config.sh

usage() {
  echo ""
  echo "usage: run-test-load.sh <system> <key-size> <value-size>"
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

${RESTART_SYSTEM}

cap -S test_driver=$SYSTEM -S client_multiplier=12 -S test_args="--random --test-name=$TEST_NAME --value-data=$VALUE_DATA --output-dir=$REPORT_DIR write $KEY_SIZE $VALUE_SIZE $DATA_SIZE" run_test

#${CLEAN_SYSTEM}
