#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL="$HT_HOME/bin/ht shell"
SCRIPT_DIR=`dirname $0`
MAX_KEYS=${MAX_KEYS:-"500000"}
RS1_PIDFILE=$HT_HOME/run/RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/RangeServer.rs2.pid
RS1_PORT=26001
RS2_PORT=26002
RUN_DIR=`pwd`

. $HT_HOME/bin/ht-env.sh

. $SCRIPT_DIR/utilities.sh

start_master_and_rangeservers() {
    local INDUCER1_ARG=$1
    shift
    local INDUCER2_ARG=$1
    shift
    local CONFIG=$1
    shift

    $HT_HOME/bin/ht-start-master.sh $CONFIG

    $HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS1_PIDFILE \
        --Hypertable.RangeServer.ProxyName=rs1 \
        --Hypertable.RangeServer.Port=$RS1_PORT $INDUCER1_ARG $CONFIG \
        2>&1 >> rangeserver.rs1.compaction-exception-$TEST.output &

    $HT_HOME/bin/ht RangeServer --verbose --pidfile=$RS2_PIDFILE \
        --Hypertable.RangeServer.ProxyName=rs2 \
        --Hypertable.RangeServer.Port=$RS2_PORT $INDUCER2_ARG $CONFIG \
        2>&1 >> rangeserver.rs2.compaction-exception-$TEST.output &
}

stop_master_and_rangeservers() {
    # stop master
    echo 'shutdown;quit;' | $HYPERTABLE_HOME/bin/ht master --batch
    # wait for master shutdown
    wait_for_server_shutdown master "master" "$@"
    if [ $? != 0 ] ; then
        stop_server master
    fi

    # stop rs1
    echo "shutdown; quit;" | $HT_HOME/bin/ht rangeserver localhost:$RS1_PORT
    kill -9 `cat $RS1_PIDFILE`
    \rm -f $RS1_PIDFILE

    # stop rs2
    echo "shutdown; quit;" | $HT_HOME/bin/ht rangeserver localhost:$RS2_PORT
    kill -9 `cat $RS2_PIDFILE`
    \rm -f $RS2_PIDFILE
}

wait_for_induced_failure() {
    let i=3
    while [ $i -ge 0 ]; do
        sleep 5
        fgrep "induced failure" rangeserver.rs?.compaction-exception-$TEST.output
        if [ $? -eq 0 ]; then
            return 0
        fi
        let i--
    done
    return 1
}


# Runs an individual test with two RangeServers
run_test() {
    local INDUCER1_ARG=""
    local INDUCER2_ARG=""
    local MANUAL=false
    local RELINQUISH=false
    local MANUAL_COMPACT_SERVER_PORT

    if test -n "$1" ; then
        INDUCER1_ARG="--induce-failure=$1"
        if [[ "$1" == *manual* ]]; then
            MANUAL=true
        fi
        if [[ "$1" == *relinquish* ]]; then
            RELINQUISH=true
        fi
        MANUAL_COMPACT_SERVER_PORT=$RS1_PORT
    fi        
    shift

    if test -n "$1" ; then
        INDUCER2_ARG="--induce-failure=$1"
        if [[ "$1" == *manual* ]]; then
            MANUAL=true
        fi
        if [[ "$1" == *relinquish* ]]; then
            RELINQUISH=true
        fi
        MANUAL_COMPACT_SERVER_PORT=$RS2_PORT
    fi        
    shift

    $HT_HOME/bin/ht-start-test-servers.sh --no-rangeserver --no-master \
        --no-thriftbroker --clear --FsBroker.DisableFileRemoval=true

    \rm -f rangeserver.rs1.compaction-exception-$TEST.output
    \rm -f rangeserver.rs2.compaction-exception-$TEST.output

    CONFIG="--Hypertable.RangeServer.Maintenance.Interval=100\
            --Hypertable.Master.Split.SoftLimitEnabled=false\
            --Hypertable.Failover.Quorum.Percentage=30\
            --Hypertable.RangeServer.Range.SplitSize=1M"

    start_master_and_rangeservers "$INDUCER1_ARG" "$INDUCER2_ARG" "$CONFIG"

    # If this is a relinquish test, start the ThriftBroker
    if [ $RELINQUISH == "true" ]; then
        $HT_HOME/bin/ht-start-thriftbroker.sh
    fi

    # create table
    $HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

    # write data 
    $HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
        --max-keys=$MAX_KEYS --row-seed=$ROW_SEED --table=LoadTest \
        --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=2M \
        --Hypertable.Mutator.FlushDelay=500 
    if [ $? != 0 ] ; then
        echo "Problem loading table 'LoadTest', exiting ..."
        save_failure_state
        kill_rs 1 2
        $HT_HOME/bin/ht-stop-servers.sh
        exit 1
    fi

    if [ $RELINQUISH == "true" ]; then
        let j=3
        while [ $j -gt 0 ]; do
            # Move a range from rs1 to rs2
            HQL_COMMAND=`$SCRIPT_DIR/generate_range_move.py rs1`
            echo $HQL_COMMAND
            echo $HQL_COMMAND | $HT_HOME/bin/ht shell --batch --Hypertable.Request.Timeout=20000
            if [ $? -ne 0 ] ; then
                stop_master_and_rangeservers
                $HT_HOME/bin/ht-stop-servers.sh
                exit 1
            fi
            wait_for_induced_failure
            if [ $? -eq 0 ] ; then
                break
            fi
            let j--
        done
        if [ $j -eq 0 ]; then
            stop_master_and_rangeservers
            $HT_HOME/bin/ht-stop-servers.sh
            echo "Falure was not induced!"
            exit 1
        fi
    else
        if [ $MANUAL == "true" ]; then
            echo "COMPACT RANGES ALL;" | $HT_HOME/bin/ht rangeserver --batch localhost:$MANUAL_COMPACT_SERVER_PORT
        fi
        wait_for_induced_failure
        if [ $? -ne 0 ] ; then
            stop_master_and_rangeservers
            $HT_HOME/bin/ht-stop-servers.sh
            echo "Falure was not induced!"
            exit 1
        fi
    fi

    # dump keys
    dump_keys LoadTest

    # Get rid of any core file generated
    \rm -f core.*

    # stop all servers
    stop_master_and_rangeservers
    $HT_HOME/bin/ht-stop-servers.sh
}

if [ $TEST == 1 ] ; then
    \rm -f errors.txt
fi

\rm -f report.txt

# generate golden output file
gen_test_data

let j=1
[ $TEST == $j ] && run_test "compact-manual-1:throw:0" "compact-manual-1:throw:0"
let j+=1
[ $TEST == $j ] && run_test "" "compact-manual-2:throw:0"
let j+=1
[ $TEST == $j ] && run_test "compact-split-1:throw:0" "compact-split-1:throw:0"
let j+=1
[ $TEST == $j ] && run_test "" "compact-split-2:throw:0"
let j+=1
[ $TEST == $j ] && run_test "compact-relinquish-1:throw:0" "compact-relinquish-1:throw:0"
let j+=1
[ $TEST == $j ] && run_test "compact-relinquish-2:throw:0" ""

exit 0
