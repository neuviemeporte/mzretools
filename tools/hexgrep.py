#!/usr/bin/python3
#
# searches for a sequence of hex bytes in a file, produces offset in case of a match, supports wildcard bytes
#
import sys

def debug(msg):
    DEBUG = 0
    if DEBUG:
        print(msg)

pattern=sys.argv[1]
file=sys.argv[2]
skip=int(sys.argv[3],16)

byteseq=[]
po=0
while po < len(pattern):
    hs = pattern[po:po+2]
    val = -1
    if hs != '??':
        val = int(hs,16)
    byteseq.append(val)
    po += 2

for i in byteseq:
    debug(hex(i))

# too lazy to write Boyer-Moore
with open(file, 'rb') as f:
    pos = skip
    f.seek(pos, 0)
    while s := f.read(len(byteseq)):
        debug(f"pos = {pos}, read s = {s}")
        match = True
        for i in range(len(byteseq)):
            if byteseq[i] == -1:
                continue
            if s[i] != byteseq[i]:
                debug(f"mismatch at idx {i}")
                match = False
                break
        if match:
            print(hex(pos - skip))
            exit(0)
        else:
            pos += 1
            f.seek(pos, 0)

# not found
exit(1)



