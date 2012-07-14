#!/usr/bin/env bash
#
# Copyright (C) 2007-2012 Hypertable, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

if [ -e /proc/sys/vm/swappiness ] ; then 
  echo "/proc/sys/vm/swappiness = `cat /proc/sys/vm/swappiness`"
fi

# The installation directory
export HYPERTABLE_HOME=$(cd `dirname "$0"`/.. && pwd)
. $HYPERTABLE_HOME/bin/ht-env.sh

usage() {
  echo ""
  echo "usage: start-rangeserver.sh [OPTIONS] [<server-options>]"
  echo ""
  echo "OPTIONS:"
  echo "  --valgrind  run range server with valgrind"
  echo "  --heapcheck run range server with google-perf-tools Heapcheck"
  echo ""
}

while [ "$1" != "${1##[-+]}" ]; do
  case $1 in
    --valgrind)
      VALGRIND="valgrind -v --log-file=vg --leak-check=full --num-callers=20 "
      shift
      ;;
    --heapcheck)
      HEAPCHECK="env HEAPCHECK=normal"
      shift
      ;;
    --heapprofile)
      HEAPCHECK="env HEAPPROFILE=/tmp/rs-$$.hprof"
      shift
      ;;
    *)
      break
      ;;
  esac
done

max_retries=200
report_interval=10
start_server rangeserver Hypertable.RangeServer Hypertable.RangeServer "$@"
