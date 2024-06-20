#!/bin/bash

f1=$1
f2=$2
[ -f "$f1" ] || { echo "file does not exist: $f1"; exit 1; }
[ -f "$f2" ] || { echo "file does not exist: $f2"; exit 1; }

xxd $f1 > $f1.hex
xxd $f2 > $f2.hex
vimdiff $f1.hex $f2.hex
rm $f1.hex $f2.hex
