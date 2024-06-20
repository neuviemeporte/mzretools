#!/bin/bash
testexe=$1
[ "$testexe" ] || { echo "Syntax: test.sh <exe_to_run>"; exit 1; }
[ -f "$testexe" ] || { echo "Executable $testexe does not exist!"; exit 1; }

testdir=$(dirname $testexe)
testfile=$(basename $testexe)
[ -d "$testdir" ] || { echo "Executable directory '$testdir' invalid"; exit 1; }
batfile=$testdir/test.bat
#echo "Placing call to $testfile in $batfile"
cat > $batfile << EOF
$testfile > test.log
EOF
SDL_VIDEODRIVER=dummy dosbox -conf test.conf $batfile -exit &> /dev/null
logfile=$testdir/test.log
if [ -f "$logfile" ]; then
    cat $logfile
    rm $logfile
else
    echo "Unable to find test log!"
fi
if [ -f "$batfile" ]; then
    rm $batfile
else
    echo "Unable to remove temporary batfile"
fi
