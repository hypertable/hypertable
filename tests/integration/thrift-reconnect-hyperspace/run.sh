#!/usr/bin/env bash

HT_TEST_DFS=${HT_TEST_DFS:-local}
if [ ! -z $HT_TEST_THRIFT_TP ]; then
	transport=--thrift-transport=${HT_TEST_THRIFT_TP}
fi
set -v

HT_HOME=${INSTALL_DIR:-"$HOME/hypertable/current"}

$HT_HOME/bin/ht-start-test-servers.sh --clear
$HT_HOME/bin/ht-stop-thriftbroker.sh
$HT_HOME/bin/ht-start-thriftbroker.sh ${transport}

sleep 5;
$HT_HOME/bin/ht-stop-servers.sh --no-thriftbroker 
sleep 3;
$HT_HOME/bin/ht-start-all-servers.sh --no-thriftbroker $HT_TEST_DFS
sleep 12;

cd ${THRIFT_CPP_TEST_DIR};
./${TEST_BIN} ${HT_TEST_THRIFT_TP}
