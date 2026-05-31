#!/usr/bin/env python3
import sys
from output import error, debug, info, setDebug
import traceback
import re

setDebug(True)

class Segment:
    def __init__(self, name, type, addr):
        self.name = name
        self.type = type
        self.addr = int(addr, 16)

class Routine:
    def __init__(self, name, segname, nf, start, end):
        self.name = name
        self.segname = segname
        self.nf = nf
        self.start = int(start, 16)
        self.end = int(end, 16)
        self.reach = []
        self.unreach = []
    def addReachable(self, start, end):
        self.reach.append((int(start, 16), int(end, 16)))
    def addUnreachable(self, start, end):
        self.unreach.append((int(start, 16), int(end, 16)))
    def toString(self):
        ret = f"{self.name}: segment {self.segname}, nf: {self.nf}, start: {self.start:x}, end: {self.end:x}"
        reach = ""
        unreach = ""
        for r in self.reach:
            reach += f" {r[0]:x}-{r[1]:x}"
        for u in self.unreach:
            unreach += f" {u[0]:x}-{u[1]:x}"
        if reach:
            ret += f", reachable:{reach}"
        if unreach:
            ret += f", unreachable:{unreach}"
        return ret

def parseMap(map_path):
    name_re = "[_a-zA-Z0-9]+"
    hexnum_re = "[0-9a-f]+"
    routine_re = re.compile(f"^({name_re}): ({name_re}) (NEAR|FAR) ({hexnum_re})-({hexnum_re}) (.*)")
    block_re = re.compile(f"(R|U)({hexnum_re})-({hexnum_re})")
    segment_re = re.compile(f"({name_re}) (CODE|DATA) ({hexnum_re})")
    map1 = open(map_path)
    routines = []
    segments = []
    for line in map1:
        debug(f"Line: {line.rstrip('\r\n')}")
        if (match := routine_re.match(line)) is not None:
            name = match.group(1)
            segname = match.group(2)
            nf = match.group(3)
            start = match.group(4)
            end = match.group(5)
            stuff = match.group(6)
            r = Routine(name, segname, nf, start, end)
            for token in stuff.split():
                if (match := block_re.match(token)) is not None:
                    ru = match.group(1)
                    bstart = match.group(2)
                    bend = match.group(3)
                    if ru == 'R':
                        r.addReachable(bstart, bend)
                    elif ru == 'U':
                        r.addUnreachable(bstart, bend)
            routines.append(r)
            debug(f"Found routine: {r.toString()}")
        elif (match := segment_re.match(line)) is not None:
            name = match.group(1)
            type = match.group(2)
            addr = match.group(3)
            segments.append(Segment(name, type, addr))
    return routines, segments

def findMatch(needle, haystack):
    for candidate in haystack:
        pass

def segAddr(segname, segs):
    for s in segs:
        if s.name == segname:
            return s.addr
    return None

def main(map1_path, map2_path):
    debug(f"Opening map1: {map1_path}, map2: {map2_path}")
    r1, s1 = parseMap(map1_path)
    r2, s2 = parseMap(map2_path)
    debug(f"Found {len(r1)} routines in {map1_path} and {len(r2)} in {map2_path}")
    for r in r1:
        debug("Searching for match of routine")
        rs = segAddr(r.segname, s1)
        debug("")
        m = findMatch(r, r2)
        if m is not None:
            r1.remove(r)
            r2.remove(m)




if __name__ == '__main__':
    argc = len(sys.argv)
    if argc != 3:
        info("Syntax: map_xref.py mapfile1 mapfile2")
        sys.exit(1)
    map1 = sys.argv[1]
    map2 = sys.argv[2]
    success = True
    try:
        main(map1, map2)
    except RuntimeError:
        success = False
    except:
        success = False
        traceback.print_exc()
    finally:
        if not success:
            sys.exit(1)