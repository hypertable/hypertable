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

# shut down range servers
kill -9 `cat $HT_HOME/run/RangeServer.*.pid`
\rm -f $HT_HOME/run/RangeServer.*.pid

# Dumping cores slows things down unnecessarily for normal test runs
#ulimit -c 0

start_master() {
    local INDUCER_ARG=$1
    shift
    set_start_vars Master
    check_pidfile $pidfile && return 0

    check_server --config=${SCRIPT_DIR}/test.cfg master
    if [ $? != 0 ] ; then
        $HT_HOME/bin/ht Master --verbose --pidfile=$HT_HOME/run/Master.pid \
            --config=${SCRIPT_DIR}/test.cfg $INDUCER_ARG 2>&1 > master.output.$TEST&
        wait_for_server_up master "$pidname" --config=${SCRIPT_DIR}/test.cfg
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
        let port-=1
    done
    sleep 1
    kill -9 `cat $HT_HOME/run/RangeServer.rs?.pid`
    \rm -f $HT_HOME/run/RangeServer.rs?.pid
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

save_failure_state() {
  touch $HT_HOME/run/debug-op
  tar czvf fs-backup.tgz $HT_HOME/fs/local
  ARCHIVE_DIR="archive-"`date | sed 's/ /-/g'`
  mkdir $ARCHIVE_DIR
  mv fs-backup.tgz core.* *.output.$TEST $HT_HOME/run/monitoring/mop.dot $ARCHIVE_DIR
  if [ -e Testing/Temporary/LastTest.log.tmp ] ; then
    cp Testing/Temporary/LastTest.log.tmp $ARCHIVE_DIR/LastTest.log.tmp
  elif [ -e ../../../Testing/Temporary/LastTest.log.tmp ] ; then
    cp ../../../Testing/Temporary/LastTest.log.tmp $ARCHIVE_DIR/LastTest.log.tmp
  fi
  for f in $HT_HOME/run/*.pid; do
    pstack `cat $f` > $ARCHIVE_DIR/`basename $f | sed 's/\.pid//g'`.stack ;
  done
  sleep 60
  mv $HT_HOME/run/op.output $ARCHIVE_DIR
  echo "Failure state saved to directory $ARCHIVE_DIR"
  exec 1>&-
  sleep 86400
}



# Runs an individual test with two RangeServers; the master goes down
# during recovery
run_test() {
    local RESTART_RANGESERVERS_ARG
    if [ $1 == "--restart-rangeservers" ]; then
        RESTART_RANGESERVERS_ARG=$1
        shift
    fi
    local MASTER_INDUCED_FAILURE=$1
    local i port WAIT_ARGS INDUCED_FAILURE PIDFILE PORT
    shift
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
    if [ ! -z "${RESTART_RANGESERVERS_ARG}" ]; then
        RESTART_RANGESERVERS_ARG="${RESTART_RANGESERVERS_ARG} ${RS_COUNT}"
    fi

    echo "Running test $TEST." >> report.txt
    let i=1
    while [ $i -le $RS_COUNT ] ; do
        echo "$i: ${INDUCED_FAILURE[$i]}" >> report.txt
        let i+=1
    done

    stop_range_servers $RS_COUNT
    $HT_HOME/bin/ht-stop-servers.sh

    sleep 10

    $HT_HOME/bin/ht-start-test-servers.sh --no-master --no-rangeserver \
        --no-thriftbroker --clear --FsBroker.DisableFileRemoval=true

    # start master-launcher script in background. it will restart the
    # master as soon as it crashes
    local INDUCER_ARG MASTER_EXIT
    if test -n "$MASTER_INDUCED_FAILURE" ; then
        INDUCER_ARG="--induce-failure=$MASTER_INDUCED_FAILURE"
    fi        
    echo $MASTER_INDUCED_FAILURE | grep ":exit:" > /dev/null
    if [ $? == 0 ] ; then
        MASTER_EXIT=true
        set_start_vars Master
        $SCRIPT_DIR/master-launcher.sh $RESTART_RANGESERVERS_ARG $INDUCER_ARG > master.output.$TEST 2>&1 &
        wait_for_server_up master "$pidname" --config=${SCRIPT_DIR}/test.cfg        
    else
        start_master $INDUCER_ARG
    fi

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
            --config=${SCRIPT_DIR}/test.cfg 2>&1 > rangeserver.rs$j.output.$TEST &
        # wait for RS_METRICS table to get created at rs1
        if [ $j -eq 1 ]; then
            let k=0
            fgrep RS_METRICS master.output.$TEST
            while [ $? -ne 0 ] && [ $k -lt 10 ]; do
                sleep 3
                let k++
                fgrep RS_METRICS master.output.$TEST
            done
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
    
    sleep 10
    $HT_SHELL --namespace 'sys' --batch --exec \
        'select Location from METADATA revs=1;' > locations.$TEST

  # kill rs1
    stop_rs 1
    
  # wait for recovery to complete 
    wait_for_recovery "rs1"
    let j=2
    while [ $j -le $RS_COUNT ] ; do
        if test -n "${INDUCED_FAILURE[$j]}" ; then
            wait_for_recovery "rs$j"          
        fi
        let j+=1
    done

    if test -n "$MASTER_EXIT" ; then
        fgrep "CRASH" master.output.$TEST
        if [ $? != 0 ] ; then
            echo "ERROR: Failure was not induced in Master."
            save_failure_state
            exit 1
        fi
    fi
    
  # dump keys
    $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
        | grep -v "hypertable" | grep -v "Waiting for connection to Hyperspace" > dbdump.$TEST
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
            | grep -v "hypertable" | grep -v "Waiting for connection to Hyperspace" > dbdump.$TEST.again
        exec 1>&-
        sleep 86400
    else
        echo "Test $TEST PASSED." >> report.txt
    fi

    sleep 5

    # shut down range servers
    kill -9 `cat $HT_HOME/run/RangeServer.*.pid`
    \rm -f $HT_HOME/run/RangeServer.*.pid

    # Make sure all ranges have been acknowledged
    let j=2
    while [ $j -le $RS_COUNT ] ; do
        if test -z "${INDUCED_FAILURE[$j]}" ; then
            $HT_HOME/bin/ht metalog_dump /hypertable/servers/rs$j/log/rsml | fgrep -v "PHANTOM" | fgrep "load_acknowledged=false"
            if [ $? == 0 ] ; then
                echo "ERROR: Unacknowledged ranges"
                exit 1
            fi
        fi
        let j+=1
    done

    # shut down remaining servers
    $HT_HOME/bin/ht-stop-servers.sh

}

if [ $TEST == 0 ] ; then
    \rm -f errors.txt
fi

\rm -f report.txt

gen_test_data

let j=1
[ $TEST == $j ] && run_test "recover-server-ranges-root-initial-1:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-root-initial-2:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-root-load-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test --restart-rangeservers "recover-server-ranges-root-load-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-root-replay-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-root-prepare-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-root-commit-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-root-ack-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-metadata-load-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-metadata-replay-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-metadata-prepare-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-metadata-commit-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-metadata-ack-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-load-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-replay-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test --restart-rangeservers "recover-server-ranges-user-replay-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-prepare-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test --restart-rangeservers "recover-server-ranges-user-prepare-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-commit-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test --restart-rangeservers "recover-server-ranges-user-commit-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-ack-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-1:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-2:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-3:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-4:exit:0" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-phantom-load-ranges:throw:0" "" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-replay-fragments:throw:0" "" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-phantom-prepare-ranges:throw:0" "" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-phantom-commit-ranges:throw:0" "" "" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-acknowledge-load:throw:0" "" "" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-load-user:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-load-2:exit:0" "" "phantom-load-user:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "replay-fragments-user-0:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-replay-2:exit:0" "" "replay-fragments-user-0:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "replay-fragments-user-1:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-update-user:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-replay-2:exit:0" "" "phantom-update-user:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-update-metadata:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-prepare-ranges-user-1:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-prepare-ranges-user-2:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-prepare-2:exit:0" "" "phantom-prepare-ranges-user-2:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-prepare-ranges-user-3:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-commit-user-1:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-commit-user-2:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "recover-server-ranges-user-commit-2:exit:0" "" "phantom-commit-user-2:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-commit-user-3:exit:0" ""
let j+=1
[ $TEST == $j ] && run_test "" "" "phantom-commit-user-4:exit:0" ""

echo ""
echo "**** TEST REPORT ****"
echo ""
cat report.txt
grep FAILED report.txt > /dev/null && exit 1
exit 0
