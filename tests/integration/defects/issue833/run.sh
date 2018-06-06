#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
SCRIPT_DIR=`dirname $0`

echo "======================="
echo "Defect #833"
echo "======================="

### JavaThriftClient no longer require hadoop
# make sure hypertable jar files are copied into lib/java
#$HT_HOME/bin/ht-set-hadoop-distro.sh $2
###

VERSION=$1
THRIFT_VERSION=$2
CP="$HT_HOME/lib/java/ht-thriftclient-${VERSION}-v${THRIFT_VERSION}-bundled.jar:$HT_HOME/lib/java/ht-thriftclient-hadoop-tools-${VERSION}-v${THRIFT_VERSION}-bundled.jar"
java -ea -cp $CP org.hypertable.hadoop.mapreduce.ScanSpec
