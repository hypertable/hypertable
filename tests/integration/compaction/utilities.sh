DIGEST="openssl dgst -md5"
MASTER_LOG=$HT_HOME/log/Master.log
MASTER_PIDFILE=$HT_HOME/run/Master.pid
ROW_SEED=1

save_failure_state() {
  ARCHIVE_DIR="archive-"`date | sed 's/ /-/g'`
  mkdir $ARCHIVE_DIR
  \rm -f $HT_HOME/run/op.output
  touch $HT_HOME/run/debug-op
  ps auxww | fgrep -i hyper | fgrep -v java > $ARCHIVE_DIR/ps-output.txt
  cp $HT_HOME/log/* $ARCHIVE_DIR
  pstack `cat $HT_HOME/run/Master.pid` > $ARCHIVE_DIR/master-stack.txt
  sleep 60
  cp $HT_HOME/run/op.output $ARCHIVE_DIR
  cp rangeserver.* $ARCHIVE_DIR
  cp rangeserver.*.output $ARCHIVE_DIR
  cp master.* $ARCHIVE_DIR
}


start_master() {
  set_start_vars Master
  check_pidfile $pidfile && return 0

  check_server --config=${SCRIPT_DIR}/test.cfg master
  if [ $? != 0 ] ; then
      $HT_HOME/bin/ht Master --verbose --pidfile=$MASTER_PIDFILE \
          --config=${SCRIPT_DIR}/test.cfg 2>&1 > $MASTER_LOG&
    wait_for_server_up master "$pidname" --config=${SCRIPT_DIR}/test.cfg
  else
    echo "WARNING: $pidname already running."
  fi
}

wait_for_server_connect() {
    grep RS_METRICS $HT_HOME/log/Master.log
    while [ $? -ne 0 ] ; do
        sleep 3
        grep RS_METRICS $HT_HOME/log/Master.log
    done
}

wait_for_recovery() {
  local n=0
  local s
  if [ $# -gt 0 ]; then
      s="Leaving RecoverServer $1 state=COMPLETE"
  else
      s="Leaving RecoverServer [a-zA-Z0-9]+ state=COMPLETE"
  fi
  egrep "$s" $HT_HOME/log/Master.log
  while [ $? -ne "0" ]
  do
    (( n += 1 ))
    if [ "$n" -gt "300" ]; then
      echo "wait_for_recovery: time exceeded"
      save_failure_state
      exit 1
    fi
    sleep 2
    egrep "$s" $HT_HOME/log/Master.log
  done
}

gen_test_data() {
    if [ ! -s golden_dump.$MAX_KEYS.md5 ] ; then
        $HT_HOME/bin/ht load_generator update --spec-file=$SCRIPT_DIR/data.spec \
            --max-keys=$MAX_KEYS --row-seed=$ROW_SEED --table=LoadTest \
            --stdout | cut -f1 | tail -n +2 | sort -u > golden_dump.$MAX_KEYS.txt
        $DIGEST < golden_dump.$MAX_KEYS.txt > golden_dump.$MAX_KEYS.md5
        #\rm -f golden_dump.$MAX_KEYS.txt
    fi
}

dump_keys() {
    echo "SELECT * FROM '$1' KEYS_ONLY MAX_VERSIONS 1 INTO FILE '$1.$TEST.tsv';" | \
        $HT_HOME/bin/ht shell -l error --batch
    if [ $? != 0 ] ; then
        echo "Problem dumping table 'LoadTest', exiting ..."
        save_failure_state
        exit 1
    fi
    tail -n +2 $1.$TEST.tsv | $DIGEST > $1.md5
    diff golden_dump.$MAX_KEYS.md5 $1.md5
    if [ $? != 0 ] ; then
        echo "Test $TEST FAILED."
        echo "SELECT * FROM '$1' KEYS_ONLY MAX_VERSIONS 1 INTO FILE '$1.again.tsv';" | \
            $HT_HOME/bin/ht shell -l error --batch
        exec 1>&-
        sleep 86400
    fi
}


stop_rs() {
    local port
    let port=38059+$1
    echo "shutdown; quit;" | $HT_HOME/bin/ht rangeserver localhost:$port
    kill -9 `cat $HT_HOME/run/RangeServer.rs$1.pid`
    \rm -f $HT_HOME/run/RangeServer.rs$1.pid
}

kill_rs() {
    for num in "$@"; do
        kill -9 `cat $HT_HOME/run/RangeServer.rs${num}.pid`
        \rm -f $HT_HOME/run/RangeServer.rs${num}.pid
    done
}

kill_all_rs() {
    kill -9 `cat $HT_HOME/run/RangeServer.rs*.pid`
    \rm -f $HT_HOME/run/RangeServer.rs*.pid
    kill -9 `cat $HT_HOME/run/RangeServer.pid`
    \rm -f $HT_HOME/run/RangeServer.pid
}
