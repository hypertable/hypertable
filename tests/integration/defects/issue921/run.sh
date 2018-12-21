#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
SCRIPT_DIR=`dirname $0`

echo "======================="
echo "Defect #921"
echo "======================="
echo HT_HOME: $HT_HOME
echo SCRIPT_DIR:$SCRIPT_DIR
echo "starting HT w/ Thrift"
$HT_HOME/bin/ht-start-test-servers.sh --clear

echo "preparing the table"

cat ${SCRIPT_DIR}/test.hql | $HT_HOME/bin/ht hypertable \
        --namespace / --no-prompt --test-mode \
        > test.output

### JavaThriftClient no longer require hadoop
# make sure hypertable jar files are copied into lib/java
# $HT_HOME/bin/ht-set-hadoop-distro.sh $1
#############
#
# javac/java failed on test01 with this:
#
# Exception in thread "main" java.lang.IncompatibleClassChangeError: class
# org.hypertable.thriftgen.ClientService$Client has interface
# org.apache.thrift.TServiceClient as super class
#
# To fix this, manually add the thriftbroker-*.jar as the first jar file
# in the classpath
#

echo "compiling"
VERSION=$1
THRIFT_VERSION=$2
CP="$HT_HOME/lib/java/ht-thriftclient-${VERSION}-v${THRIFT_VERSION}-bundled.jar:$HT_HOME/lib/java/ht-thriftclient-hadoop-tools-${VERSION}-v${THRIFT_VERSION}-bundled.jar"
javac -classpath $CP -d . $SCRIPT_DIR/TestInputOutput.java

echo "running"
java -ea -classpath $CP:. -Dhypertable.mapreduce.thriftbroker.framesize=10240 TestInputOutput >> test.output

diff test.output $SCRIPT_DIR/test.golden
if [ "$?" -ne "0" ]
then
    # If stdout has additional "Received count" (not explained)
	diff test.output $SCRIPT_DIR/test_wrcv_c.golden
	if [ "$?" -ne "0" ]
	then
		echo "output differs"
		exit 1
	fi
fi

echo "success"
exit 0
