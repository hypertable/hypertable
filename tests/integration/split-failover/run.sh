#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL="$HT_HOME/bin/ht shell"
SCRIPT_DIR=`dirname $0`
DATA_SEED=42
DATA_SIZE=${DATA_SIZE:-"2000000"}
DIGEST="openssl dgst -md5"
RUN_DIR=`pwd`

. $HT_HOME/bin/ht-env.sh

# get rid of all old logfiles
\rm -rf $HT_HOME/log/*

# Dumping cores slows things down unnecessarily for normal test runs
#ulimit -c 0

save_failure_state() {
  tar czvf fs-backup.tgz $HT_HOME/fs/local
  ARCHIVE_DIR="archive-"`date | sed 's/ /-/g'`
  mkdir $ARCHIVE_DIR
  mv fs-backup.tgz dbdump.* rangeserver.rs?.output.$TEST master.output.$TEST $ARCHIVE_DIR
  cp $HT_HOME/run/monitoring/mop.dot $ARCHIVE_DIR
  cp -r $HT_HOME/log $ARCHIVE_DIR
  if [ -e Testing/Temporary/LastTest.log.tmp ] ; then
    ln Testing/Temporary/LastTest.log.tmp $ARCHIVE_DIR/LastTest.log.tmp
  elif [ -e ../../../Testing/Temporary/LastTest.log.tmp ] ; then
    ln ../../../Testing/Temporary/LastTest.log.tmp $ARCHIVE_DIR/LastTest.log.tmp
  fi
  echo "Failure state saved to directory $ARCHIVE_DIR"
  #exec 1>&-
  #sleep 86400
}

start_master() {
  set_start_vars Master
  check_pidfile $pidfile && return 0

  check_server --config=${SCRIPT_DIR}/test.cfg master
  if [ $? != 0 ] ; then
      $HT_HOME/bin/htMaster --verbose \
        --pidfile=$HT_HOME/run/Master.pid \
        --Hypertable.Master.Gc.Interval=30000 \
        --Hypertable.RangeServer.Range.SplitSize=18K \
        --Hypertable.Master.Split.SoftLimitEnabled=false \
        --Hypertable.RangeServer.Range.MetadataSplitSize=20K \
        --Hypertable.Failover.GracePeriod=30000 \
        --Hypertable.Failover.Quorum.Percentage=40 \
        --Hypertable.Connection.Retry.Interval=3000 > master.output.$TEST 2>&1 &
    wait_for_server_up master "$pidname"
  else
    echo "WARNING: $pidname already running."
  fi
}

wait_for_recovery() {
  local id=$1
  local n=0
  local s="Leaving RecoverServer $id state=COMPLETE"
  egrep "$s" master.output.$TEST
  while [ $? -ne "0" ]
  do
    (( n += 1 ))
    if [ "$n" -gt "300" ]; then
      echo "wait_for_recovery: time exceeded"
      save_failure_state
      exit 1
    fi
    sleep 2
    egrep "$s" master.output.$TEST
  done
}

gen_test_data() {
    if [ ! -s golden_dump.md5 ] ; then
        $HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
            --max-keys=$DATA_SIZE --row-seed=$DATA_SEED --table=FailoverTest \
            --stdout | cut -f1 | tail -n +2 | sort -u > golden_dump.txt
        $DIGEST < golden_dump.txt > golden_dump.md5
    fi
}

stop_range_servers() {
    local port
    let port=15869+$1
    while [ $port -ge 15870 ] ; do
        echo "shutdown; quit;" | $HT_HOME/bin/ht rangeserver localhost:$port
        let rsnum=port-15869
        kill -9 `cat $HT_HOME/run/RangeServer.rs${rsnum}.pid`
        \rm -f $HT_HOME/run/RangeServer.rs${rsnum}.pid
        let port-=1
    done
}

kill_range_servers() {
    let rsnum=1
    while [ $rsnum -le $1 ] ; do
        kill -9 `cat $HT_HOME/run/RangeServer.rs${rsnum}.pid`
        \rm -f $HT_HOME/run/RangeServer.rs${rsnum}.pid
        let rsnum++
    done
}

stop_rs() {
    local port
    let port=15869+$1
    echo "shutdown; quit;" | $HT_HOME/bin/ht rangeserver localhost:$port
    kill -9 `cat $HT_HOME/run/RangeServer.rs$1.pid`
    \rm -f $HT_HOME/run/RangeServer.rs$1.pid
}

kill_rs() {
    kill -9 `cat $HT_HOME/run/RangeServer.rs$1.pid`
    \rm -f $HT_HOME/run/RangeServer.rs$1.pid
}


# Runs an individual test with two RangeServers; the master goes down
# during recovery
run_test() {
    local i port WAIT_ARGS INDUCED_FAILURE PIDFILE PORT
    let i=1
    while [ $# -gt 0 ] ; do
        INDUCED_FAILURE[$i]=$1
        PIDFILE[$i]=$HT_HOME/run/RangeServer.rs$i.pid
        let port=15869+$i
        PORT[$i]=$port
        let i+=1
        shift
    done
    let RS_COUNT=i-1

    echo "Running test $TEST." >> report.txt
    let i=1
    while [ $i -le $RS_COUNT ] ; do
        echo "$i: ${INDUCED_FAILURE[$i]}" >> report.txt
        let i+=1
    done

    stop_range_servers $RS_COUNT

    $HT_HOME/bin/ht-start-test-servers.sh --no-master --no-rangeserver \
        --no-thriftbroker --clear --FsBroker.DisableFileRemoval=true

    start_master

    local j
    let j=1
    while [ $j -le $RS_COUNT ] ; do
        INDUCER_ARG=
        if test -n "${INDUCED_FAILURE[$j]}" ; then
            INDUCER_ARG=--induce-failure=${INDUCED_FAILURE[$j]}
        fi
        $HT_HOME/bin/ht RangeServer --verbose --pidfile=${PIDFILE[$j]} \
            --Hypertable.RangeServer.ProxyName=rs$j \
            --Hypertable.RangeServer.Port=${PORT[$j]} $INDUCER_ARG \
            --Hypertable.RangeServer.CellStore.DefaultBlockSize=1K \
            --Hypertable.RangeServer.MaintenanceThreads=8 \
            --Hypertable.RangeServer.Maintenance.Interval=100 2>&1 > rangeserver.rs$j.output.$TEST &
        if [ $j -eq 1 ] ; then
            sleep 5
        fi
        let j+=1
    done

    $HT_SHELL --batch < $SCRIPT_DIR/create-test-table.hql
    if [ $? != 0 ] ; then
        echo "Unable to create table 'failover-test', exiting ..."
        save_failure_state
        exit 1
    fi

    $HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
        --max-keys=$DATA_SIZE --row-seed=$DATA_SEED --table=FailoverTest \
        --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=100K \

    if [ $? != 0 ] ; then
        echo "Problem loading table 'FailoverTest', exiting ..."
        save_failure_state
        exit 1
    fi
    
  # wait for recovery to complete 
    #wait_for_recovery "rs1"
    let j=2
    while [ $j -le $RS_COUNT ] ; do
        if test -n "${INDUCED_FAILURE[$j]}" ; then
            wait_for_recovery "rs$j"          
        fi
        let j+=1
    done

  # dump keys
    $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
        | grep -v "hypertable" > dbdump.$TEST
    if [ $? != 0 ] ; then
        echo "Problem dumping table 'failover-test', exiting ..."
        save_failure_state
        exit 1
    fi
    
    $DIGEST < dbdump.$TEST > dbdump.md5
    diff golden_dump.md5 dbdump.md5 > out
    if [ $? != 0 ] ; then
        echo "Test $TEST FAILED." >> report.txt
        echo "Test $TEST FAILED." >> errors.txt
        cat out >> report.txt
        touch error
        $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
            | grep -v "hypertable" > dbdump.$TEST.again
        exec 1>&-
        sleep 86400
    else
        echo "Test $TEST PASSED." >> report.txt
    fi

    sleep 10

    # Make sure all ranges have been acknowledged
    let i=0
    let error_count=1
    while [ $error_count -ne 0 ] && [ $i -le 6 ] ; do
        sleep 10
        let error_count=0
        let j=2
        while [ $j -le $RS_COUNT ] ; do
            if test -z "${INDUCED_FAILURE[$j]}" ; then
                $HT_HOME/bin/ht metalog_dump /hypertable/servers/rs$j/log/rsml | fgrep "load_acknowledged=false"
                if [ $? -eq 0 ] ; then
                    let error_count++
                fi
            fi
            let j+=1
        done
        let i++
    done

    # shut down all servers
    kill -9 `cat $HT_HOME/run/Master.pid`
    \rm -f $HT_HOME/run/Master.pid
    kill_range_servers $RS_COUNT
    $HT_HOME/bin/ht-stop-servers.sh --no-master --no-rangeserver

    if [ $error_count -ne 0 ]; then
        echo "ERROR: Unacknowledged ranges"
        save_failure_state
        exit 1
    fi
}

if [ $TEST == 0 ] ; then
    rm -f errors.txt
fi

rm -f report.txt

gen_test_data

let j=1
[ $TEST == $j ] && run_test "metadata-split-1:exit:1" "" ""
let j+=1
[ $TEST == $j ] && run_test "metadata-split-2:exit:1" "" ""
let j+=1
[ $TEST == $j ] && run_test "metadata-split-3:exit:1" "" ""
let j+=1
[ $TEST == $j ] && run_test "metadata-split-4:exit:1" "" ""
let j+=1
[ $TEST == $j ] && run_test "metadata-load-range-3:exit:2" "" ""
let j+=1
[ $TEST == $j ] && run_test "metadata-load-range-4:exit:2" "" ""
let j+=1
[ $TEST == $j ] && run_test "user-range-acknowledge-load-pause-1:pause(10000):3;user-range-acknowledge-load-1:exit:3" "" ""
let j+=1


echo ""
echo "**** TEST REPORT ****"
echo ""
cat report.txt
grep FAILED report.txt > /dev/null && exit 1
exit 0
