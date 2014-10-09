#!/usr/bin/env bash

export HT_HOME=$(cd `dirname "$0"`/.. && pwd)

declare -a Distros=('cdh3' 'cdh4' 'cdh5');

usage() {
  echo ""
  echo "usage: set-hadoop-distro.sh <distro>"
  echo ""
  echo "This script will copy the jar files for <distro> into the primary jar"
  echo "directory ($HT_HOME/lib/java)"
  echo "so that Hypertable will run on top of a Hadoop installation of type"
  echo "<distro>.  Supported values for <distro> are:  ${Distros[@]}."
  echo "The default distribution is cdh3."
  echo ""
}

if [ "$#" -eq 0 ]; then
  usage
  exit 1
fi

DISTRO=
for distro in ${Distros[@]}; do
    if [ $1 == $distro ]; then
        DISTRO=$1
        shift
        break;
    fi
done

# Check for valid distro
if [ -z $DISTRO ]; then
    usage
    echo "error: unrecognized distro '$1'"
    exit 1;
fi


# Remove existing distro files
for distro in ${Distros[@]}; do

    if [ ! -d $HT_HOME/lib/java/$distro ]; then
        echo "$HT_HOME/lib/java/$distro does not exist, exiting..."
        exit 1
    fi

    cd $HT_HOME/lib/java/$distro
    for jar in `ls -1`; do
        \rm -f $HT_HOME/lib/java/$jar
    done 

done

\cp -f $HT_HOME/lib/java/$DISTRO/* $HT_HOME/lib/java

echo $DISTRO > $HT_HOME/conf/hadoop-distro

echo "Hypertable successfully configured for Hadoop $DISTRO"

exit 0
