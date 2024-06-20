#!/bin/bash

dir1=$1
dir2=$2

[[ -z "$dir1" || -z "$dir2" ]] && { echo "Syntax: $(basename $0) dir1 dir2"; exit 1; }
for d in "$dir1" "$dir2"; do
	[ -d "$d" ] || { echo "Directory $d does not exist!"; exit 1; }
done

log='fingerprint.log'
for f in "$dir1/$log" "$dir2/$log"; do
	[ -f "$f" ] || { echo "Logfile $f does not exist! Run fingerprint.sh to generate."; exit 1; }
done

vimdiff "$dir1/$log" "$dir2/$log"
