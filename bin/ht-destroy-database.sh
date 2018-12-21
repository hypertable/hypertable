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

if [ "e$RUNTIME_ROOT" == "e" ]; then
  RUNTIME_ROOT=$HYPERTABLE_HOME
fi

DISPLAY_USAGE=

if [ $# == 1 ]; then
  case $1 in
    -h|--help)
      DISPLAY_USAGE=true
      shift
      ;;
  esac
fi

TOPLEVEL="/"`$HYPERTABLE_HOME/bin/ht get_property Hypertable.Directory $@`"/"
TOPLEVEL=`echo $TOPLEVEL | tr -s "/" | sed 's/.$//g'`

usage() {
  local REAL_ROOT=$RUNTIME_ROOT
  readlink $RUNTIME_ROOT > /dev/null
  if [ $? -eq 0 ]; then
    REAL_ROOT="`dirname $RUNTIME_ROOT`/`readlink $RUNTIME_ROOT`"
  fi
  echo
  echo "usage: ht-destroy-database.sh [OPTIONS] [<server-options>]"
  echo
  echo "OPTIONS:"
  echo "  -h,--help             Display usage information"
  echo "  -i,--interactive      Displays a warning and waits for 10 seconds before"
  echo "                        proceeding to give the operator a chance to hit"
  echo "                        ctrl-c to exit"
  echo
  echo "This script starts by shutting down (on localhost) the ThriftBroker,"
  echo "RangeServer, Master, and Hyperspace.  It then check to see if the"
  echo "FsBroker is running and if not, it removes the following local files"
  echo "and (contents of) directories:"
  echo
  echo "  $REAL_ROOT/hyperspace"
  echo "  $REAL_ROOT/run/log_backup/rsml"
  echo "  $REAL_ROOT/run/log_backup/mml"
  echo "  $REAL_ROOT/run/location"
  echo "  $REAL_ROOT/run/last-fs"
  echo "  $REAL_ROOT/run/STATUS.*"
  echo "  $REAL_ROOT/fs"
  echo
  echo "If the FsBroker is running, this script will recursively remove the"
  echo "Hypertable directories in the brokered file system by issuing the"
  echo "following commands to the fsbroker client program:"
  echo
  echo "  rmdir $TOPLEVEL/servers"
  echo "  rmdir $TOPLEVEL/tables"
  echo
  echo "It then removes the same local files and directories listed above for"
  echo "the case when the FsBroker is not running.  Lastly, the FsBroker is"
  echo "shut down.  The <server-options> argument is passed to all programs and"
  echo "scripts invoked by this script."
  echo
}

clear_local() {
    /bin/rm -rf $RUNTIME_ROOT/hyperspace/* $RUNTIME_ROOT/run/log_backup/rsml/* \
        $RUNTIME_ROOT/run/log_backup/mml/* $RUNTIME_ROOT/run/location \
        $RUNTIME_ROOT/run/last-fs $RUNTIME_ROOT/run/STATUS.* \
        $RUNTIME_ROOT/fs/*
    echo "Cleared $RUNTIME_ROOT/hyperspace"
    echo "Cleared $RUNTIME_ROOT/run"
    echo "Cleared $RUNTIME_ROOT/fs"
}

if [ -n "$DISPLAY_USAGE" ]; then
  usage
  exit 0
fi

confirm=y
if tty > /dev/null && [ $# == 1 ]; then
  case $1 in
    -i|--interactive)
      echo "Are you sure you want to destroy the database (on default ports)?"
      echo "Will proceed in 10 seconds, (Ctrl-C to quit)"
      shift
      sleep 10
      ;;
  esac
fi

# Stop servers other than fsbroker
stop_server thriftbroker rangeserver master hyperspace
sleep 1
wait_for_server_shutdown thriftbroker "ThriftBroker" "$@" &
wait_for_server_shutdown rangeserver "RangeServer" "$@" &
wait_for_server_shutdown master "Master" "$@" &
wait_for_server_shutdown hyperspace "Hyperspace" "$@" &
wait

case $confirm in
  y|Y)
    #
    # Clear state
    #
    $HYPERTABLE_HOME/bin/ht-check-fsbroker.sh --silent "$@"
    if [ $? != 0 ] ; then
      echo "ERROR: FsBroker not running, database not cleaned"
      # remove local stuff anyway.
      clear_local
      exit 1
    fi

    $HYPERTABLE_HOME/bin/ht fsbroker --timeout 60000 -e "rmdir $TOPLEVEL/servers" "$@"
    $HYPERTABLE_HOME/bin/ht fsbroker --timeout 60000 -e "rmdir $TOPLEVEL/tables" "$@"
    echo "Removed fs:/$TOPLEVEL/servers"
    echo "Removed fs:/$TOPLEVEL/tables"
    clear_local
    ;;
  *) echo "Database not cleared";;
esac

#
# Stop fsbroker
#
$HYPERTABLE_HOME/bin/ht-stop-fsbroker.sh "$@"
