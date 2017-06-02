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

# The installation directory
export HYPERTABLE_HOME=$(cd `dirname "$0"`/.. && pwd)
. $HYPERTABLE_HOME/bin/ht-env.sh

usage() {
  local REAL_HOME=$HYPERTABLE_HOME
  readlink $HYPERTABLE_HOME > /dev/null
  if [ $? -eq 0 ]; then
    REAL_HOME="`dirname $HYPERTABLE_HOME`/`readlink $HYPERTABLE_HOME`"
  fi
  echo
  echo "usage: ht-java-run.sh [OPTIONS] <class-name> [<program-args>]"
  echo
  echo "OPTIONS:"
  echo "  -h,--help               Display usage information"
  echo "  --add-to-classpath <f>  Adds <f> to classpath"
  echo "  --pidfile <f>           Write JVM process ID to file <f>"
  echo "  --debug                 Run JVM with the follwoing debug options:"
  echo
  echo "  -Xrunjdwp:transport=dt_socket,address=8000,server=y,suspend=n -Xdebug"
  echo
  echo "This script runs a Java program with the Hypertable jar files added to"
  echo "the classpath.  It first checks to see if the following file exists:"
  echo
  echo "  $REAL_HOME/conf/hadoop-distro"
  echo
  echo "If it does not exist, an error message will be written to the console"
  echo "indicating that ht-set-hadoop-distro.sh needs to be run, and the script"
  echo "will exit with status 1.  Otherwise, it checks the modification time"
  echo "of the hadoop-distro file with the modification time of the first jar"
  echo "file it finds in $REAL_HOME/lib/java/"
  echo "If the modification time of the hadoop-distro file is newer, it assumes"
  echo "that the Hadoop distro has changed and runs ht-set-hadoop-distro.sh to"
  echo "make sure the appropriate Hadoop jar files are copied into"
  echo "$REAL_HOME/lib/java/"
  echo
  echo "The script then populates a CLASSPATH variable by concatenating"
  echo "all of the jar files supplied with the --add-to-classpath option"
  echo "with all of the jar files found within the"
  echo "$REAL_HOME/lib/java/ directory.  Once the CLASSPATH"
  echo "variable is setup, the JVM is invoked in one of the following two"
  echo "ways, depending on whether or not the JAVA_HOME environment variable"
  echo "is set:"
  echo
  echo '  exec $JAVA_HOME/bin/java $DEBUG_ARGS -classpath "$CLASSPATH" "$@"'
  echo
  echo '  exec java $DEBUG_ARGS -classpath "$CLASSPATH" "$@"'
  echo
}

if [ -f /etc/profile.d/custom_env.sh ]; then
	source /etc/profile;
	source ~/.bashrc;
fi

# Parse and remove ht-java-run.sh specific arguments
DEBUG_ARGS=

# Setup CLASSPATH
CLASSPATH="${HYPERTABLE_HOME}"

while [ $# -gt 1 ] ; do
  if [ "--pidfile" == "$1" ] ; then
    shift
    echo "$$" > $1
    shift
  elif [ "--debug" == "$1" ] ; then
    DEBUG_ARGS="-Xrunjdwp:transport=dt_socket,address=8000,server=y,suspend=n\
                -Xdebug"
    shift
  elif [ "--add-to-classpath" == "$1" ] ; then
    shift
    CLASSPATH=${CLASSPATH}:$1
    shift
  elif [ "--verbose" == "$1" ] ; then
    shift
  else
    break
  fi
done

if [ $# == 1 ]; then
  case $1 in
    -h|--help)
      usage
      exit 0
      ;;
  esac
fi

# Make sure configured for Hadoop distro
DISTRO=
if [ -e $HYPERTABLE_HOME/conf/hadoop-distro ]; then
  DISTRO=`cat $HYPERTABLE_HOME/conf/hadoop-distro`
fi

if [ -z "$DISTRO" ]; then
    echo "No Hadoop distro is configured.  Run the following script to"
    echo "configure:"
    echo ""
    echo "$HYPERTABLE_HOME/bin/ht-set-hadoop-distro.sh"
    exit 1
fi

# so that filenames w/ spaces are handled correctly in loops below
IFS=

DISTRO_NEEDS_SETTING=0
JAR_COUNT=`ls -1 $HYPERTABLE_HOME/lib/java/*.jar | wc -l`
if [ $JAR_COUNT -eq 0 ]; then
    DISTRO_NEEDS_SETTING=1
else
    JAR=`ls -1 $HYPERTABLE_HOME/lib/java/*.jar | head -1`
    SYSTEM=`uname -s`
    if [ $SYSTEM == "Linux" ]; then
        CONF_DATE=`stat -t -c '%Y' $HYPERTABLE_HOME/conf/hadoop-distro`
        JAR_DATE=`stat -t -c '%Y' $JAR`
        if [[ $CONF_DATE > $JAR_DATE ]]; then
            DISTRO_NEEDS_SETTING=1
        fi
    elif [ $SYSTEM == "Darwin" ]; then
        CONF_DATE=`stat -f '%B' $HYPERTABLE_HOME/conf/hadoop-distro`
        JAR_DATE=`stat -f '%B' $JAR`
        if [[ $CONF_DATE > $JAR_DATE ]]; then
            DISTRO_NEEDS_SETTING=1
        fi
    else
        DISTRO_NEEDS_SETTING=1
    fi
fi

if [ $DISTRO_NEEDS_SETTING -eq 1 ]; then
    $HYPERTABLE_HOME/bin/ht-set-hadoop-distro.sh $DISTRO
fi


# add lib/java to CLASSPATH
for f in $HYPERTABLE_HOME/lib/java/*.jar; do
  CLASSPATH=${CLASSPATH}:$f;
done

unset IFS

#
# run it
#
if [ "$JAVA_HOME" != "" ] ; then
  exec $JAVA_HOME/bin/java $DEBUG_ARGS -classpath "$CLASSPATH" "$@"
else
  exec java $DEBUG_ARGS -classpath "$CLASSPATH" "$@"
fi
