#!/usr/bin/env bash
#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Hypertable. If not, see <http://www.gnu.org/licenses/>
#

if [ -z $HT_HOME ]; then
	export HT_HOME=$(cd `dirname "$0"`/.. && pwd)
fi

declare -a Distros=(); #'apache-1' 'apache-2' 'cdh-3' 'cdh-4' 'cdh-5' 'hdp-2'

prefix=$HT_HOME/lib/java;
for distro in $prefix/ht-fsbroker-*.jar; do
	distro=${distro:-1}  
	Distros+=( "${distro:${#prefix}+22:-12} " )
done;	
echo $Distros



usage() {
  local REAL_HOME=$HT_HOME
  readlink $HT_HOME > /dev/null
  if [ $? -eq 0 ]; then
    REAL_HOME="`dirname $HT_HOME`/`readlink $HT_HOME`"
  fi
  VERSION=`basename $REAL_HOME`
  # THRIFT_JAR=`ls $REAL_HOME/lib/java/common/libthrift-*`
  # THRIFT_JAR=`basename $THRIFT_JAR`
  echo
  echo "usage: ht-set-hadoop-distro.sh [OPTIONS] <distro>"
  echo
  echo "OPTIONS:"
  echo "  -h,--help  Display usage information"
  echo
  echo "This script will copy the jar files for Hadoop distribution specified by"
  echo "<distro> into the primary jar directory ($REAL_HOME/lib/java)."
  echo "This ensures that the correct Java programs (e.g. Hadoop FsBroker) and"
  echo "associated jar files will be used for the Hadoop distribution upon which"
  echo "Hypertable will be running.  The currently supported values for <distro>"
  echo "include:"
  echo
  for distro in "${Distros[@]}"; do
    echo "  $distro"
  done
  echo
  echo "It sets up convenience links in the primary jar"
  echo "directory for the hypertable and thrift jar files.  For example:"
  echo
  echo "  htFsbroker.jar -> ht-fsbroker-$VERSION-distro-distro_version-bundled.jar"
  echo
}

if [ "$#" -eq 0 ] || [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
  usage
  exit 0
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



# Setup symlinks
pushd .
cd $HT_HOME/lib/java
\ln -sf ht-fsbroker-[0-9]*-$DISTRO-bundled.jar htFsbroker.jar
popd

echo $DISTRO > $HT_HOME/conf/hadoop-distro

echo "Hypertable successfully configured for Hadoop $DISTRO"

exit 0
