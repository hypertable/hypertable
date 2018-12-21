#!/usr/bin/env bash

HYPERTABLE_HOME=/opt/hypertable/sanjit/current

CONFIG=$PWD/test-sequential-load-count.cfg
DATA_CONFIG=$PWD/data-gen.cfg

RUN_ONCE="true"

while [ "$1" != "${1##[-+]}" ]; do
    case $1 in
        --repeat)
            RUN_ONCE="false"
            shift
            ;;
        *)
            echo $"$0: Invalid option - $1"
            exit 1;;
    esac
done

DFS="hadoop"
if [ "$#" -ne 0 ]; then
  DFS=$1
  shift
fi

while true; do
  rm count.output
  cap -S config=$CONFIG -S dfs=$DFS dist
  cap -S config=$CONFIG -S dfs=$DFS cleandb
  cap -S config=$CONFIG -S dfs=$DFS start

  sleep 5

  $HYPERTABLE_HOME/bin/ht shell --no-prompt --config=$CONFIG < create-table.hql
  if [ $? != 0 ] ; then
     echo "Unable to create table 'SequentialLoadCountTest', exiting ..."
     exit 1
  fi

  $HYPERTABLE_HOME/bin/ht_load_generator update --table SequentialLoadCountTest --spec-file $DATA_CONFIG

  if [ $? != 0 ] ; then
     echo "Problem loading table 'SequentialLoadCountTest', exiting ..."
     exit 1
  fi

  $HYPERTABLE_HOME/bin/ht shell --no-prompt --config=$CONFIG < shutdown.hql
  if [ $? != 0 ] ; then
     echo "Unable to shutdown RangeServers exiting ..."
     exit 1
  fi

  cap -S config=$CONFIG -S dfs=$DFS stop
  cap -S config=$CONFIG -S dfs=$DFS start

  sleep 15

  $HYPERTABLE_HOME/bin/ht shell --batch --config=$CONFIG < dump-table.hql > dbdump

  wc -l dbdump > count.output
  diff count.output count.golden
  if [ $? != 0 ] ; then
     echo "Test failed, exiting ..."
     exit 1
  fi
  echo "Test passed."
  if [ $RUN_ONCE == "true" ] ; then
      exit 0
  fi
done
