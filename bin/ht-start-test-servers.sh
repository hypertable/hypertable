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

INSTALL_DIR=${INSTALL_DIR:-$(cd `dirname $0`/.. && pwd)}
HT_TEST_FS=${HT_TEST_FS:-local}

usage_exit() {
  echo
  echo "usage: ht-start-test-servers.sh [OPTIONS] [<global-options>]"
  echo
  echo "OPTIONS:"
  echo "  --clear                  Destroy existing database before starting servers"
  echo "  -h,--help                Display usage information"
  echo "  --no-master              Do not launch the Master"
  echo "  --no-rangeserver         Do not launch the RangeServer"
  echo "  --no-thriftbroker        Do not launch the ThriftBroker"
  echo "  --valgrind-hyperspace    Pass --valgrind option to ht-start-hyperspace.sh"
  echo "  --valgrind-master        Pass --valgrind option to ht-start-master.sh"
  echo "  --heapcheck-rangeserver  Pass --heapcheck option to ht-start-rangeserver.sh"
  echo "  --valgrind-rangeserver   Pass --valgrind option to ht-start-rangeserver.sh"
  echo "  --valgrind-thriftbroker  Pass --valgrind option to ht-start-thriftbroker.sh"
  echo "  --opt--Any  			   Pass --Any option to ht start all-servers"
  echo
  echo "Starts all Hypertable processes on localhost.  By default, the local filesystem"
  echo "broker is started.  Alternatively, the environment varibale HT_TEST_FS can be"
  echo "set to any valid filesystem specifier to control which filesystem broker to"
  echo "start.  Valid filesystem specifiers include \"local\", \"hadoop\", \"ceph\", \"mapr\","
  echo "and \"qfs\".  This value is passed directly to the ht-start-fsbroker.sh script."
  echo
  echo "The script first stops all Hypertable processes with a call to"
  echo "ht-stop-servers.sh.  If the --clear option is specified, the script will instead"
  echo "start the filesystem broker and then destroy the database with a call to"
  echo "ht-destroy-database.sh."
  echo
  echo "Once the Hypertable processes are stopped, the hypertable processes are started"
  echo "with a call to ht-start-all-servers.sh."
  echo
  echo "The <global-options> are passed to all scripts run by this script."
  echo
  exit 0
}

while [ $# -gt 0 ]; do
  case $1 in
    --clear)              clear=1;;
    -h|--help)            usage_exit;;
    --val*|--no*|--heap*) opts[${#opts[*]}]=$1;;
    --opt-*) 			  tmp=$1;opts[${#opts[*]}]=${tmp:5};;
    *)                    break;;
  esac
  shift
done

if [ "$clear" ]; then
	$INSTALL_DIR/bin/ht-start-fsbroker.sh $HT_TEST_FS
	$INSTALL_DIR/bin/ht destroy-database "$@"
	
	PIDS1=`ps auxww | fgrep "$INSTALL_DIR/bin/ht" | grep -v "ht-start-test-servers.sh" | fgrep -v grep | tr -s "[ ]" | cut -f2 -d' '`
	PIDS2=`ps auxww | fgrep "org.hypertable.FsBroker.hadoop.main" | fgrep -v grep | tr -s "[ ]" | cut -f2 -d' '`
	if [ -n "$PIDS1" ] || [ -n "$PIDS2" ]; then
		COUNT=`echo "$PIDS1 $PIDS2" | wc -w`
		echo "Sending SIGKILL to $COUNT processes ..."
		kill -9 `echo "$PIDS1 $PIDS2"`
	fi
else
  $INSTALL_DIR/bin/ht stop servers "$@"
fi
  

$INSTALL_DIR/bin/ht start all-servers "${opts[@]}" $HT_TEST_FS "$@"
sleep 3
