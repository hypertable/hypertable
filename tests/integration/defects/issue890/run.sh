#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
SCRIPT_DIR=`dirname $0`

VERSION=$1
THRIFT_VERSION=$2

echo "======================="
echo "Defect #890"
echo "======================="

echo HT_HOME: $HT_HOME
echo SCRIPT_DIR:$SCRIPT_DIR
echo VERSION: $VERSION
echo THRIFT_VERSION:$THRIFT_VERSION

echo "compiling"
CP="$HT_HOME/lib/java/ht-thriftclient-${VERSION}-v${THRIFT_VERSION}-bundled.jar"
javac -classpath $CP -d . $SCRIPT_DIR/TestSerializers.java

echo "running"
java -ea -classpath $CP:. TestSerializers
