#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
SCRIPT_DIR=`dirname $0`

echo "======================="
echo "Defect #922"
echo "======================="

# delete old monitoring data
\rm -rf $HT_HOME/run/monitoring
\rm -rf $HT_HOME/log/*

# start the cluster and load it with data  #  --opt--thrift-transport=zlib 
$HT_HOME/bin/ht-start-test-servers.sh --clear
sleep 5

# create the table
$HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

# and write a few cells, just so that the ThriftBroker generates and caches
# its internal handles
# if test-servers started with --opt--thrift-transport=zlib 
# ht_load_generator require --thrift-transport=zlib  
$HT_HOME/bin/ht ht_load_generator update --spec-file=${SCRIPT_DIR}/data.spec \
        --table=LoadTest --thrift --max-keys=10 2>&1
		
if [ $? -ne "0" ];
then
    echo "load generator failed"
    exit 1
fi

# now drop and re-create the table
$HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

# and again write a few cells; this must succeed
$HT_HOME/bin/ht ht_load_generator update --spec-file=${SCRIPT_DIR}/data.spec \
        --table=LoadTest --thrift --max-keys=10000 2>&1

if [ $? -ne "0" ];
then
    echo "load generator failed"
    exit 1
fi

echo "Success"
exit 0
