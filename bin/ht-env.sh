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

ulimit -c unlimited
ulimit -v unlimited

if [ "e$RUNTIME_ROOT" == "e" ]; then
  RUNTIME_ROOT=$HYPERTABLE_HOME
fi

die() {
  echo "$@"
  exit 1
}

server_pidfile() {
  case $1 in
    hyperspace)         echo $RUNTIME_ROOT/run/Hyperspace.pid;;
    dfsbroker)          echo $RUNTIME_ROOT/run/DfsBroker.*.pid | grep -v "*";;
    fsbroker)           echo $RUNTIME_ROOT/run/FsBroker.*.pid | grep -v "*";;
    master)             echo $RUNTIME_ROOT/run/Master.pid;;
    rangeserver)        echo $RUNTIME_ROOT/run/RangeServer.pid;;
    thriftbroker)       echo $RUNTIME_ROOT/run/ThriftBroker*.pid | grep -v "*";;
    testclient)         echo $RUNTIME_ROOT/run/Hypertable.TestClient*.pid | grep -v "*";;
    testdispatcher)     echo $RUNTIME_ROOT/run/Hypertable.TestDispatcher.pid;;
    *) echo "unknown";  echo "ERROR: unknown service: $1" >&2; return 1
  esac
}

check_pidfile() {
  for f in $@; do
    pid=`cat $f 2> /dev/null`
    service=`basename $f .pid`
    if [ "$pid" ]; then
      ret=`ps -fp $pid | grep $pid`
      [ $? != 0 ] && return 1
      echo "$service appears to be running ($pid):"
    else
      return 1
    fi
    echo $ret
  done
}

kill_from_pidfiles() {
    for i do
        if [ -f "$i" ] ; then
            pid=`cat $i`
            echo "Killing `basename $i` $pid"
            kill -9 $pid
            rm $i
        fi
    done
}

stop_server() {
  pidre='.pid$'
  for server in $@; do
    if [[ $server =~ $pidre ]]; then
      pidfiles=$server
    else
      pidfiles=`server_pidfile $server`
    fi
    for pidfile in $pidfiles ; do
      kill_from_pidfiles $pidfile
    done
  done
}

should_wait() {
  ret=$1
  become=$2
  case $become in
    come*up)    return $((!$ret));;
    shutdown)   return $(($ret));;
    *)          echo "ERROR: unexpected expectation: $become";;
  esac
  exit 1
}

show_success() {
  server_desc=$1
  become=$2
  case $become in
    come*up)    echo "Started $server_desc";;
    shutdown)   echo "Shutdown $server_desc complete";;
    *)          echo "ERROR: unexpected expectation: $become";;
  esac
}

check_server() {
  $HYPERTABLE_HOME/bin/ht serverup --silent "$@" >& /dev/null
}

wait_for_server() {
  become=$1; shift
  server=$1; shift
  server_desc=$1; shift
  max_retries=${max_retries:-40}
  report_interval=${report_interval:-5}
  address=`$HYPERTABLE_HOME/bin/ht serverup --display-address=true $server`

  if [ "$address" ]; then
    address=" ($address)";
  fi

  check_server "$@" $server
  ret=$?
  retries=0
  while should_wait $ret "$become" && [ $retries -lt $max_retries ]; do
    let retries=retries+1
    let report=retries%$report_interval
    [ $report == 0 ] && echo "Waiting for $server_desc$address to $become..."
    sleep 1
    check_server "$@" $server
    ret=$?
  done
  if should_wait $ret "$become"; then
    echo "ERROR: $server_desc did not $become"
    check_pidfile `server_pidfile $server`
    return 1
  else
    show_success "$server_desc" "$become"
    return 0
  fi
}

wait_for_server_up() {
  wait_for_server "come up" "$@"
  check_startlog $? $startlog
}

wait_for_server_shutdown() {
  wait_for_server "shutdown" "$@"
}


wait_for_ready() {
  server=$1; shift
  server_desc=$1; shift
  max_retries=${max_retries:-40}
  report_interval=${report_interval:-5}
  retries=${retries:-0}
  ready_status=${ready_status:-1}
  local STATUS_TEXT
  local status

  STATUS_TEXT=`$HYPERTABLE_HOME/bin/ht-check-${server}.sh "$@"`
  status=$?

  while [ ${status} -gt ${ready_status} ] && [ $retries -lt $max_retries ]; do
    sleep 1
    let retries=retries+1
    let report=retries%$report_interval
    STATUS_TEXT=`$HYPERTABLE_HOME/bin/ht-check-${server}.sh "$@"`
    status=$?
    if [ $report == 0 ] && [ ${status} -gt ${ready_status} ]; then
      local reason=`echo $STATUS_TEXT | cut -f2 -d- | sed -e 's/^[[:space:]]*//'`
      echo "Waiting for $server_desc to come up ( $reason ) ..."
    fi
  done
  if [ ${status} -le ${ready_status} ]; then
    echo "${server_desc} Started"
    status=0
  else
    echo $STATUS_TEXT
  fi
  [ -s $startlog ] && cat $startlog
  rm -f $startlog
  return $status
}


wait_for_critical() {
  server=$1; shift
  server_desc=$1; shift
  max_retries=${max_retries:-40}
  report_interval=${report_interval:-5}

  retries=0
  $HYPERTABLE_HOME/bin/ht-check-${server}.sh --silent "$@"
  ret=$?
  while [ $ret -ne 2 ] && [ $retries -lt $max_retries ]; do
    let retries=retries+1
    let report=retries%$report_interval
    [ $report == 0 ] && echo "Waiting for $server_desc to shutdown ..."
    sleep 1
    $HYPERTABLE_HOME/bin/ht-check-${server}.sh --silent "$@"
    ret=$?
  done
  if [ $ret -ne 2 ]; then
    echo "ERROR: $server_desc did not shutdown"
    check_pidfile `server_pidfile $server`
    return 1
  else
    echo "Shutdown $server_desc complete"
    return 0
  fi
}


set_start_vars() {
  pidfile=$RUNTIME_ROOT/run/$1.pid
  logfile=$RUNTIME_ROOT/log/$1.log
  startlog=/tmp/start-$1$$.log
  logger="$RUNTIME_ROOT/sbin/cronolog --link $logfile \
      $RUNTIME_ROOT/log/archive/%Y-%m/%d/$1.log"
}

check_startlog() {
  [ -s $2 ] && cat $2
  [ $1 != 0 ] && return 1
  rm -f $2
}

exec_server() {
  servercmd=$1; shift
  if [ "$logger" ]; then
    rm -f ${logfile};
    ($HEAPCHECK $VALGRIND $HYPERTABLE_HOME/bin/$servercmd --pidfile $pidfile "$@" 2>&1 |
        $logger) &> $startlog &
  else
    $HEAPCHECK $VALGRIND $HYPERTABLE_HOME/bin/$servercmd --pidfile $pidfile "$@" \
        &> $logfile &
  fi
}

start_server() {
  server=$1; shift
  servercmd=$1; shift
  pidname=$1; shift
  set_start_vars $pidname
  check_pidfile $pidfile && return 0

  check_server "$@" $server
  if [ $? != 0 ] ; then
    exec_server $servercmd --verbose "$@"
    wait_for_server_up $server "$pidname" $@
  else
    echo "WARNING: $pidname already running."
  fi
}

start_server_no_check() {
  server=$1; shift
  servercmd=$1; shift
  pidname=$1; shift
  set_start_vars $pidname
  check_pidfile $pidfile && return 0
  exec_server $servercmd --verbose "$@"
}

# Sanity check
[ "$HYPERTABLE_HOME" ] || die "ERROR: HYPERTABLE_HOME is not set"
#versionre='/([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+(\.pre[0-9]*)?(\.[a-fA-F0-9]+)?|current)$'
#[[ $HYPERTABLE_HOME =~ $versionre ]] ||
#  die "ERROR: Invalid HYPERTABLE_HOME: $HYPERTABLE_HOME doesnt match regex ${versionre}"

# Make sure log and run directories exist
[ -d $RUNTIME_ROOT/run ] || mkdir $RUNTIME_ROOT/run
[ -d $RUNTIME_ROOT/log ] || mkdir $RUNTIME_ROOT/log

# Runtime libraries
export LD_LIBRARY_PATH="$HYPERTABLE_HOME/lib:$LD_LIBRARY_PATH"
export DYLD_LIBRARY_PATH="$LD_LIBRARY_PATH:$DYLD_LIBRARY_PATH"
