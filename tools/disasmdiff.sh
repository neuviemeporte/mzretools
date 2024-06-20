#!/bin/bash

f1=$1
f2=$2
s1=$3
s2=$4

[[ -z "$f1" || -z "$f2" ]] && { echo "Usage: $(basename $0) file1 file2 [seek1] [seek2]"; exit 1; }
[ -f "$f1" ] || { echo "file does not exist: $f1"; exit 1; }
[ -f "$f2" ] || { echo "file does not exist: $f2"; exit 1; }
[ -z "$s1" ] && s1=0
[ -z "$s2" ] && s2=0

dis1=$f1.dis
dis2=$f2.dis

ndisasm -e "$s1" "$f1" > "$dis1" || exit 1
ndisasm -e "$s2" "$f2" > "$dis2" || exit 1
[ -f "$dis1" ] || { echo "$dis1 does not exist!"; exit 1; }
[ -f "$dis2" ] || { echo "$dis2 does not exist!"; exit 1; }
vimdiff "$f1.dis" "$f2.dis"
rm "$f1.dis" "$f2.dis"
