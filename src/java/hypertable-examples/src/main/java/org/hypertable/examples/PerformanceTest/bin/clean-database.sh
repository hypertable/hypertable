#!/usr/bin/env bash

#. ~/.ssh-agent

export BINDIR=$(cd `dirname "$0"` && pwd)
. $BINDIR/test-config.sh

usage() {
  echo ""
  echo "usage: clean-database.sh <system>"
  echo ""
  echo "This script is used to clean the database and remove all tables."
  echo "The <system> argument indicates which system to run the test"
  echo "against and can take a value of 'hypertable' or 'hbase'"
  echo ""
}

if [ "$#" -ne 1 ]; then
  usage
  exit 1
fi

SYSTEM=$1
shift

CLEAN_SYSTEM=

if [ "$SYSTEM" == "hypertable" ] ; then
    CLEAN_SYSTEM=clean_hypertable
elif [ "$SYSTEM" == "hbase" ] ; then
    CLEAN_SYSTEM=clean_hbase
else
    echo "ERROR:  Unrecognized system name '$SYSTEM'"
    exit 1
fi

${CLEAN_SYSTEM}
