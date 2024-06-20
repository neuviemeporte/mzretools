#!/bin/bash
ref=$1
obj=$2
verbosity=$3
[ "$ref" ] || ref=0x10
[ "$obj" ] || obj=0x10
[ "$verbosity" ] || verbosity='--verbose'
../dostrace/debug/mzdiff ida/start.exe:$ref build-f15-se2/start.exe:$obj $verbosity --loose --rskip 3 --cskip 1 --variant --map map/start.map