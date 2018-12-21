#!/usr/bin/env bash

echo "========================================================================"
echo "Hyper$1: SerizalizedCellsWriter test"
echo "========================================================================"

SCRIPT_DIR=`dirname $0`

echo "SCRIPT_DIR is $SCRIPT_DIR"
echo "PYTHONPATH is $PYTHONPATH"

$1 $SCRIPT_DIR/writer.py $1 $2 || exit \$?
