#!/bin/bash
#
# This is a utility script to generate hexdumps of an exe's load image and a memory image taken at runtime (e.g. from an emulator)
# so that the two can be compared for discrepancies/code corruption etc.
#

function fatal() {
    echo $1
    exit 1
}

exe=$1
img=$2
exe_loadseg=$3

[[ "$exe" && "$img" ]] || fatal "Usage: exeimgdump.sh exe_path img_path [exe_loadseg]"
[ -f "$exe" ] || fatal "Exe file $1 does not exist"
[ -f "$img" ] || fatal "Memory image file $2 does not exist"
[ "$exe_loadseg" ] || exe_loadseg=0

exeload=$(mzhdr "$exe" -l)
exesize=$(mzhdr "$exe" -s)
echo "Exe load module at $exeload, size $exesize"
exebin=$(basename $exe)
exebin=${exebin/exe/bin}
echo "Patching load segment to $exe_loadseg and writing exe load module to $exebin"
rm -f $exebin
mzhdr $exe -p $exe_loadseg $exebin || fatal "Unable to extract load module"
[ -f "$exebin" ] || fatal "Patched output file does not exist: $exebin"
exexxd=$exebin.xxd
imgxxd=$(basename $img).xxd
echo "Hexdumping exe to $exexxd"
xxd -R never $exebin $exexxd || fatal "Unable to dump exe"
echo "Hexdumping img to $imgxxd"
xxd -R never $img $imgxxd || fatal "Unable to dump image"