#!/usr/bin/env bash
#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Hypertable. If not, see <http://www.gnu.org/licenses/>
#

# The installation directory
export HYPERTABLE_HOME=$(cd `dirname "$0"`/.. && pwd)
. $HYPERTABLE_HOME/bin/ht-env.sh

RANGESERVER_OPTS=
MASTER_OPTS=
HYPERSPACE_OPTS=
THRIFTBROKER_OPTS=

START_RANGESERVER="true"
START_MASTER="true"
START_THRIFTBROKER="true"

usage() {
  echo
  echo "usage: ht-start-all-servers.sh [OPTIONS] <fs> [<global-options>]"
  echo
  echo "OPTIONS:"
  echo "  -h,--help                Display usage information"
  echo "  --no-master              Do not launch the Master"
  echo "  --no-rangeserver         Do not launch the RangeServer"
  echo "  --no-thriftbroker        Do not launch the ThriftBroker"
  echo "  --valgrind-hyperspace    Pass --valgrind option to ht-start-hyperspace.sh"
  echo "  --valgrind-master        Pass --valgrind option to ht-start-master.sh"
  echo "  --heapcheck-rangeserver  Pass --heapcheck option to ht-start-rangeserver.sh"
  echo "  --valgrind-rangeserver   Pass --valgrind option to ht-start-rangeserver.sh"
  echo "  --valgrind-thriftbroker  Pass --valgrind option to ht-start-thriftbroker.sh"
  echo "  --thrift-transport  	   Pass  option to ht-start-thriftbroker.sh"
  echo
  echo "Starts Hypertable processes on localhost.  By default, this script will start"
  echo "all hypertable processes by running the service startup scripts in the following"
  echo "order:"
  echo
  echo "  ht-start-hyperspace.sh"
  echo "  ht-start-fsbroker.sh"
  echo "  ht-start-master.sh"
  echo "  ht-start-rangeserver.sh"
  echo "  ht-start-thriftbroker.sh"
  echo
  echo "The required argument <fs> indicates which filesystem broker to start.  Valid"
  echo "values include \"local\", \"hadoop\", \"ceph\", \"mapr\", and \"qfs\". Typical usage of"
  echo "this script is to start Hypertable in standalone mode on the local filesystem,"
  echo "for example:"
  echo
  echo "  $ ht-start-all-servers.sh local"
  echo "  FsBroker (local) Started"
  echo "  Hyperspace Started"
  echo "  Master Started"
  echo "  RangeServer Started"
  echo "  ThriftBroker Started"
  echo
  echo "The <global-options> arguments are passed to all startup scripts."
  echo
}

while [ "$1" != "${1##[-+]}" ]; do
  case $1 in
    '')
      usage
      exit 1;;
    --heapcheck-rangeserver)
      RANGESERVER_OPTS="--heapcheck "
      shift
      ;;
    --valgrind-rangeserver)
      RANGESERVER_OPTS="--valgrind "
      shift
      ;;
    --heapcheck-master)
      MASTER_OPTS="--heapcheck "
      shift
      ;;
    --valgrind-master)
      MASTER_OPTS="--valgrind "
      shift
      ;;
    --valgrind-hyperspace)
      HYPERSPACE_OPTS="--valgrind "
      shift
      ;;
    --valgrind-thriftbroker)
      THRIFTBROKER_OPTS="--valgrind ${THRIFTBROKER_OPTS}"
      shift
      ;;
    --no-rangeserver)
      START_RANGESERVER="false"
      shift
      ;;
    --no-master)
      START_MASTER="false"
      shift
      ;;
    --no-thriftbroker)
      START_THRIFTBROKER="false"
      shift
      ;;
    --thrift-*)
      THRIFTBROKER_OPTS="$1 ${THRIFTBROKER_OPTS}"
      shift
      ;;
    *)
      usage
      exit 1;;
  esac
done

if [ "$#" -eq 0 ]; then
  usage
  exit 1
fi

FS=$1
shift
#
# Start Hyperspace
#
$HYPERTABLE_HOME/bin/ht-start-hyperspace.sh $HYPERSPACE_OPTS $@ &

#
# Start FsBroker
#
$HYPERTABLE_HOME/bin/ht-start-fsbroker.sh $FS $@ &

wait

#
# Start Master
#
if [ $START_MASTER == "true" ] ; then
  $HYPERTABLE_HOME/bin/ht-start-master.sh $MASTER_OPTS $@
fi

#
# Start RangeServer
#
if [ $START_RANGESERVER == "true" ] ; then
  $HYPERTABLE_HOME/bin/ht-start-rangeserver.sh $RANGESERVER_OPTS $@
fi

#
# Start ThriftBroker (optional)
#
if [ $START_THRIFTBROKER == "true" ] ; then
  if [ -f $HYPERTABLE_HOME/bin/htThriftBroker ] ; then
    $HYPERTABLE_HOME/bin/ht-start-thriftbroker.sh $THRIFTBROKER_OPTS $@
  fi
fi
