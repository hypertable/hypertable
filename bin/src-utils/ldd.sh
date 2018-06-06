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

DARWIN=

# Do ldd on several platforms
case `uname -s` in
  Darwin)     ldd='otool -L'; DARWIN="yes";;
  *)          ldd=ldd;;
esac

file=${@:1}

if [ "$DARWIN" == "yes" ] ; then
  $ldd "$file" > /tmp/ldd-step1-$$

  lineno=0
  while read line ; do
  if [ $lineno -ne 0 ] ; then
    line=`echo $line | cut -f 1 -d' '`
    expr $line : "^/" > /dev/null
    if [ $? == 0 ] ; then
      $ldd $line | tail -n+2 >> /tmp/ldd-step2-$$
    fi
  fi
  lineno=$(($lineno+1));
  done < "/tmp/ldd-step1-$$"
  head -n 1 /tmp/ldd-step1-$$
  sort /tmp/ldd-step2-$$ | uniq
  /bin/rm -f /tmp/ldd-step1-$$ /tmp/ldd-step2-$$
else
  exec $ldd "$file"
fi

