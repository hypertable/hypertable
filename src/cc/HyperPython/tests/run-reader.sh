#!/usr/bin/env bash

echo "========================================================================"
echo "Hyper$1: SerizalizedCellsReader test"
echo "========================================================================"

SCRIPT_DIR=`dirname $0`

echo "SCRIPT_DIR is $SCRIPT_DIR"
echo "PYTHONPATH is $PYTHONPATH"

$1 $SCRIPT_DIR/reader.py $1 $2 > test-reader.txt
diff test-reader.txt $SCRIPT_DIR/test-reader.golden
if [ $? != 0 ]
then
  echo "golden file differs"
  exit 1
fi
