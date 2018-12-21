#!/usr/bin/env bash
#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 3 of the
# License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

FDLIMIT=`ulimit -n`
if [ $FDLIMIT -le 1024 ]; then
  echo "WARNING Low file descriptor limit ($FDLIMIT)"
fi

# The installation directory
export HYPERTABLE_HOME=$(cd `dirname "$0"`/.. && pwd)
. $HYPERTABLE_HOME/bin/ht-env.sh

usage() {
  local REAL_HOME=$HYPERTABLE_HOME
  readlink $HYPERTABLE_HOME > /dev/null
  if [ $? -eq 0 ]; then
    REAL_HOME="`dirname $HYPERTABLE_HOME`/`readlink $HYPERTABLE_HOME`"
  fi
  echo
  echo "usage: ht-start-fsbroker.sh [OPTIONS] <fs> [<global-options>]"
  echo
  echo "OPTIONS:"
  echo "  -h,--help   Display usage information"
  echo
  echo "Starts the filesystem broker.  This script starts the filesystem broker of type"
  echo "<fs>.  Valid values for <fs> include \"local\", \"hadoop\", \"ceph\", \"mapr\", and"
  echo "\"qfs\"."
  echo
  echo "To prevent problems arising from running Hypertable on one filesystem and then"
  echo "inadvertently starting it on another, this script records the filesystem in the"
  echo "following file:"
  echo
  echo "  $REAL_HOME/run/last-fs"
  echo
  echo "When it starts up, it verifies that the <fs> argument matches the contents of"
  echo "the last-fs file and if not, it displays an error message to the console and"
  echo "exits with status code 1.  Otherwise, it checks to see if the broker is already"
  echo "running by comparing the process ID stored in \$HT_HOME/run/FsBroker.<fs>.pid"
  echo "with the process IDs of the currently running processes.  If there's no match,"
  echo "it then performs a second check with ht-check-fsbroker.sh.  If the broker"
  echo "appears to be running, the script exits with status 0."
  echo
  echo "Most of the filesystem brokers are binary executable programs that follow the"
  echo "naming convention of adding the prefix \"htFsBroker\" to the name of the"
  echo "filesystem (e.g. htFsBrokerLocal).  The Hadoop broker is implemented as a Java"
  echo "program located in the hypertable jar file and has class name"
  echo "org.hypertable.FsBroker.hadoop.main.  It is launched with the ht-java-run.sh"
  echo "script.  All brokers are passed the --verbose option and any <global-options>"
  echo "that are supplied."
  echo
  echo "The output of the broker is piped into cronolog, a log rotation program, which"
  echo "directs its output to the log subdirectory within the Hypertable installation"
  echo "directory (HT_HOME).  The main log file is named FsBroker.<fs>.log and archives"
  echo "are stored in the \$HT_HOME/log/archive directory.  The following illustrates"
  echo "what the contents of the \$HT_HOME/log directory might look like for the"
  echo "FsBroker (local) logs:"
  echo
  echo "  archive/2015-02/28/FsBroker.local.log"
  echo "  archive/2015-03/01/FsBroker.local.log"
  echo "  archive/2015-03/02/FsBroker.local.log"
  echo "  FsBroker.local.log -> archive/2015-03/02/FsBroker.local.log"
  echo
  echo "After launching the filesystem broker, the ht-check-fsbroker.sh script is run"
  echo "repeatedly until the status is OK at which point it displays a message such as"
  echo "the followng to the console and the script exits with status code 0."
  echo
  echo "  FsBroker (local) Started"
  echo
  echo "If after 40 attempts (with a one second wait in between each), the status check"
  echo "does not return OK, the status text will be written to the terminal and the"
  echo "status code is returned as the exit status of the script."
  echo
}

fs_conflict_error() {
    OLD_FS=$1
    shift
    NEW_FS=$1
    echo ""
    echo "ERROR: FS conflict"
    echo ""
    echo "You are trying to run Hypertable with the '$NEW_FS' broker"
    echo "on a system that was previously run with the '$OLD_FS' broker."
    echo ""
    if [ "$OLD_FS" == "local" ] ; then
        echo "Run the following command to remove the previous database,"
        echo "and all of its associated state, before launching with the"
        echo "'$NEW_FS' broker:"
        echo ""
        echo "$HYPERTABLE_HOME/bin/ht-stop-servers.sh ; $HYPERTABLE_HOME/bin/ht-start-fsbroker.sh $OLD_FS ; $HYPERTABLE_HOME/bin/ht-destroy-database.sh"
        echo ""
    else
        echo "To remove the previous database, and all it's associated state,"
        echo "in order to launch with the '$NEW_FS' broker, start the system"
        echo "on the old FS and then clean the database.  For example, with"
        echo "Capistrano:"
        echo ""
        echo "cap stop ; cap -S fs=$OLD_FS cleandb"
        echo ""
    fi
    echo "Alternatively, you can manually purge the database state by issuing"
    echo "the following command on each Master and Hyperspace replica machine:"
    echo ""
    echo "rm -rf $HYPERTABLE_HOME/hyperspace/* $HYPERTABLE_HOME/fs/* $HYPERTABLE_HOME/run/rsml_backup/* $HYPERTABLE_HOME/run/last-fs"
    echo ""
}

while [ $# -gt 0 ]; do
  case $1 in
    --valgrind)
      VALGRIND="valgrind -v --log-file=vg.fsbroker.%p --leak-check=full --num-callers=50 "
      shift
      ;;
    -h|--help)
      usage;
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

if [ "$#" -eq 0 ]; then
  usage
  exit 1
fi

FS=$1
shift

if [ -e $HYPERTABLE_HOME/run/last-fs ] ; then
    LAST_FS=`cat $HYPERTABLE_HOME/run/last-fs`
    if [ "$FS" != "$LAST_FS" ] ; then
        fs_conflict_error $LAST_FS $FS
        exit 1
    fi
else
    # record last FS
    echo $FS > $HYPERTABLE_HOME/run/last-fs
fi

set_start_vars FsBroker.$FS
check_pidfile $pidfile && exit 0

$HYPERTABLE_HOME/bin/ht-check-fsbroker.sh --silent "$@"
if [ $? != 0 ] ; then
  if [ "$FS" == "hadoop" ] ; then
    if [ "n$VALGRIND" != "n" ] ; then
      echo "ERROR: hadoop broker cannot be run with valgrind"
      exit 1
    fi
    exec_server ht-java-run.sh --cp-group FsBroker org.hypertable.FsBroker.hadoop.main --verbose "$@"
  elif [ "$FS" == "mapr" ] ; then
    LD_LIBRARY_PATH="${JAVA_HOME}/lib" LIBHDFS3_CONF="${HADOOP_HOME}/etc/hadoop" exec_server htFsBrokerMapr --verbose "$@"
  elif [ "$FS" == "ceph" ] ; then
    exec_server htFsBrokerCeph --verbose "$@"
  elif [ "$FS" == "local" ] ; then
    exec_server htFsBrokerLocal --verbose "$@"
  elif [ "$FS" == "qfs" ] ; then
    exec_server htFsBrokerQfs --verbose "$@"
  else
    usage
    exit 1
  fi

  wait_for_ready fsbroker "FsBroker ($FS)" "$@"
else
  echo "WARNING: FSBroker already running."
fi
