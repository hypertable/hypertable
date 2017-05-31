#!/usr/bin/env bash

echo "========================================================================"
echo "Hyper$1: SerizalizedCellsWriter test"
echo "========================================================================"

SCRIPT_DIR=`dirname $0`

echo "SCRIPT_DIR is $SCRIPT_DIR"
echo "PYTHONPATH is $PYTHONPATH"

$1 $SCRIPT_DIR/writer.py $1 $2 > test-writer.txt
#echo '-------------------------'
#cat test-writer.txt
#echo '-------------------------'
#cat $SCRIPT_DIR/test-writer.golden
#echo '-------------------------'

diff test-writer.txt $SCRIPT_DIR/test-writer.golden
if [ $? != 0 ]
then
  echo "golden file differs"
  exit 1
fi
