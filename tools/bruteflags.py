#!/usr/bin/python3
#
# this is a utility to test all combinations of a given set of compiler flags to check which of them generates the expected assembly
#
import os
import signal
import time
import sys
from itertools import combinations

def handler(signum, frame):
    print("Ctrl-c was pressed.")
    exit(1)

signal.signal(signal.SIGINT, handler)

# command to run to check if the compiler output matches the desired result (determined by zero return code)
verify_cmd = "tools/grind.sh"
# environmental variable to keep putting the flags in before running the verify command
flagvar = 'MSC_CFLAGS' 
# static flags, i.e. ones which persist for all the combinations
#flagstatic = [ '-G']
#flagstatic = [ '', '-Ot' ]
flagstatic = [ '/Ot', '/Os', '/Od', '/Ox' ]
# flags to use in isolation (no combining with others)s
flagsingle = []
#flagsingle = [ '-O2', '-O1', '-Od' ] 
# flags for checking combinations
#flagset = [ '-Oa', '-Ob', '-Oe', '-Op', '-Z' ] 
#flagset = [ '-Z -O', '-a', '-rd', '-P', '-k' ]
flagset = [ '/Ol', '/On', '/Or']
dryrun = False
combine = True

# create list of all possible combinations
flagcomb = [''] # test no options as well
if flagsingle:
    flagcomb.extend(flagsingle)
if combine:
    for s in flagstatic:
        for k in range(1, len(flagset) + 1):
            for c in combinations(flagset, k):
                flagstr = s
                if flagstr:
                    flagstr += ' '
                flagstr += ' '.join(c)
                flagcomb.append(flagstr)

# run verify command for every combination of options
startidx = 0
if len(sys.argv) > 1:
    startidx = int(sys.argv[1]) - 1
stopidx = len(flagcomb)
if len(sys.argv) > 2:
    stopidx = int(sys.argv[2])
for cidx in range(startidx, stopidx):
    c = flagcomb[cidx]
    print(f"==============================================================================")
    print(f"Combination {cidx + 1} out of {len(flagcomb)}: '{c}'")
    print(f"==============================================================================")
    if dryrun:
        continue
    os.putenv(flagvar, c)
    result = os.system(verify_cmd)
    time.sleep(0.1) # give time for ctrl-c to act
    print(f"==== verify result = {result}")
    if result == 0:
        print(f"Found matching flag combination: {c}")
        exit(0)

# no match
exit(1)