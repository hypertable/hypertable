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

usage() {
  echo
  echo "usage: ht-start-rangeserver.sh [OPTIONS] [<rangeserver-options>]"
  echo
  echo "OPTIONS:"
  echo "  -h,--help    Display usage information"
  echo "  --heapcheck  Run rangeserver with heap checking enabled"
  echo "  --valgrind   Run rangeserver with valgrind"
  echo
  echo "Starts the RangeServer on localhost.  It first checks to see if the RangeServer"
  echo "process is already running by comparing the process ID stored in"
  echo "\$HT_HOME/run/RangeServer.pid with the process IDs of the currently running"
  echo "processes.  If there's no match, it then runs a second check with"
  echo "ht-check-rangeserver.sh.  If the RangeServer appears to be running, the script"
  echo "exits with status 0.  Otherwise, the RangeServer process (htRangeServer) is"
  echo "launched with the options --verbose and <rangeserver-options>."
  echo
  echo "If the --valgrind option is supplied, then the RangeServer process is run with"
  echo "valgrind as follows:"
  echo
  echo "  valgrind -v --log-file=vg.rangeserver.%p --leak-check=full \\"
  echo "           --num-callers=50 htRangeServer ..."
  echo
  echo "If the --heapcheck option is supplied, then the RangeServer process is run with"
  echo "heap checking enabled as follows:"
  echo
  echo "  env HEAPCHECK=normal htRangeServer ..."
  echo
  echo "The output of RangeServer is piped into cronolog, a log rotation program, which"
  echo "directs its output to the log subdirectory within the Hypertable installation"
  echo "directory (HT_HOME).  The main log file is named RangeServer.log and archives"
  echo "are stored in the \$HT_HOME/log/archive directory.  The following illustrates"
  echo "what the contents of the \$HT_HOME/log directory might look like for the"
  echo "RangeServer logs:"
  echo
  echo "  archive/2015-02/28/RangeServer.log"
  echo "  archive/2015-03/01/RangeServer.log"
  echo "  archive/2015-03/02/RangeServer.log"
  echo "  RangeServer.log -> archive/2015-03/02/RangeServer.log"
  echo
  echo "After launching the RangeServer process, the ht-check-rangeserver.sh script is"
  echo "run repeatedly until it returns \"ready status\" which is the status code that"
  echo "signals that the RangeServer is ready.  The default ready status for the"
  echo "RangeServer is WARNING.  When the RangeServer starts up, it will report"
  echo "status WARNING as soon as it's ready to accept load and will continue to do so"
  echo "until all deferred initialization (loading of CellStores) is complete, after"
  echo "which it will report status OK.  During the deferred initialization period there"
  echo "will be extra load on the RangeServer and queries handled during that time may"
  echo "experience higher latency."
  echo
  echo "To force ht-start-rangeserver.sh to wait until all deferred initialization is"
  echo "complete before returning, you can set ready status to OK by adding the"
  echo "following line to the hypertable.cfg file:"
  echo
  echo "  Hypertable.RangeServer.ReadyStatus=OK"
  echo
  echo "NOTE: Setting the ready status to OK may result in the RangeServer taking a long"
  echo "time to come up."
  echo
  echo "Once the RangeServer reports the ready status, a message such as the followng is"
  echo "displayed on the console and the script exits with status code 0."
  echo
  echo "  RangeServer Started"
  echo
  echo "If after 3600 attempts (with a one second wait in between each),"
  echo "ht-check-rangeserver.sh does not report ready status, the status text will be"
  echo "written to the terminal and the status code is returned as the exit status of"
  echo "the script."
  echo
}

while [ $# -gt 0 ]; do
  case $1 in
    --valgrind)
      VALGRIND="valgrind -v --log-file=vg.rangeserver.%p --leak-check=full --num-callers=50 "
      shift
      ;;
    --heapcheck)
      HEAPCHECK="env HEAPCHECK=normal "
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

set_start_vars RangeServer
check_pidfile $pidfile && exit 0

$HYPERTABLE_HOME/bin/ht-check-rangeserver.sh --silent "$@"
if [ $? -ne 0 ] ; then
  exec_server htRangeServer --verbose "$@"
  max_retries=3600
  report_interval=30
  retries=20
  if [ `$HYPERTABLE_HOME/bin/ht get_property Hypertable.RangeServer.ReadyStatus $@ | sed 's/"//g'` == "OK" ]; then
    ready_status=0
  fi
  wait_for_ready rangeserver "RangeServer" "$@"
else
  echo "WARNING: RangeServer already running."
fi
