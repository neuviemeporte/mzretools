#!/bin/bash

script_dir=$(dirname $BASH_SOURCE)
mzre=$script_dir/../debug
mzhdr=$mzre/mzhdr

noff=0
function output() {
    ((noff == 0)) && echo "$@"
}

infile=$1
shift
[ "$infile" ] || { echo "Syntax: disasm input_file [[0x]start_offset] [--noff]"; exit 1; }
[ -f "$infile" ] || { echo "File $infile does not exist!"; exit 1; }
[ -f "$mzhdr" ] || { echo "$mzhdr does not exist!"; exit 1; }
hdrlen=$($mzhdr $infile -l)
until [ -z "$1" ]; do 
    case $1 in
    --noff)
        noff=1
        ;;
    *)
        skip=$1
        ;;
    esac
    shift; 
done
output "Header length is $hdrlen"
if [ "$skip" ]; then
    ((hdrlen+=skip))
    hdrhex=$(printf "0x%x" $hdrlen)
    output "Skipping $hdrhex bytes"
fi
if ((noff)); then
    # offset obfuscation mode - useful for comparing disassembly listings
    # squeeze spaces, remove instruction encoding column, replace offsets with obfuscation string
    ndisasm -e $hdrlen $infile | tr -s ' ' | cut -d ' ' -f 3- | sed -e 's/0x[0-9a-fA-F]\+/0x?/g'
else
    ndisasm -e $hdrlen $infile
fi