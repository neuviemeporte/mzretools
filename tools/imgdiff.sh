#!/bin/bash

exe=$1
img=$2

function fatal() {
    echo $1
    exit 1
}

[[ "$exe" && "$img" ]] || fatal "Usage: exeimgdiff.sh exe_path img_path"
[ -f "$exe" ] || fatal "Exe file $1 does not exist"
[ -f "$img" ] || fatal "Memory image file $2 does not exist"

exeload=$(mzhdr "$exe" -l)
echo "Exe load module at $exeload"
exebin=${exe/exe/bin}
echo "Extracing load module to $exebin"
dd if="$exe" of="$exebin" skip=$((exeload)) bs=1