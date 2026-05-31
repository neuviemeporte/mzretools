#!/usr/bin/env python3
import sys
from output import error, warn, debug, info, setDebug
import traceback
import re

#setDebug(True)

class Segment:
    def __init__(self, name, type, addr):
        self.name = name
        self.type = type
        self.addr = int(addr, 16)

class Routine:
    def __init__(self, name, segname, segaddr, nf, start, end):
        self.name = name
        self.segname = segname
        self.segaddr = segaddr
        self.nf = nf
        self.start = int(start, 16)
        self.end = int(end, 16)
        self.reach = []
        self.unreach = []
        self.traits = []
    def addReachable(self, start, end):
        self.reach.append((int(start, 16), int(end, 16)))
    def addUnreachable(self, start, end):
        self.unreach.append((int(start, 16), int(end, 16)))
    def addTrait(self, trait):
        self.traits.append(trait)
    def __str__(self):
        size = self.end - self.start
        ret = f"{self.name}: {self.segname}/{self.segaddr:x} {self.nf}, extents: {self.start:x}-{self.end:x}/{size:x}"
        reach = ""
        unreach = ""
        traits = ""
        for r in self.reach:
            reach += f" {r[0]:x}-{r[1]:x}"
        for u in self.unreach:
            unreach += f" {u[0]:x}-{u[1]:x}"
        for t in self.traits:
            traits += f" {t}"
        if reach:
            ret += f", reachable:{reach}"
        if unreach:
            ret += f", unreachable:{unreach}"
        if traits:
            ret += f", traits:{traits}"
        return ret
    def match(self, other):
        msg = ''
        if self.segaddr != other.segaddr or self.start != other.start:
            return False
        msg = f"Imperfect match: routine {other.name} has identical start address ({other.start:x}) as routine {self.name}"
        if self.end != other.end:
            info(msg)
            return False
        msg = f"Imperfect match: routine {other.name} has identical extents ({other.start:x}-{other.end:x}) as routine {self.name}"
        if set(self.reach) != set(other.reach) or set(self.unreach) != set(other.unreach):
            info(msg)
            return False
        return True

def segAddr(segname, segs):
    for s in segs:
        if s.name == segname:
            return s.addr
    return None

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
            segaddr = segAddr(segname, segments)
            r = Routine(name, segname, segaddr, nf, start, end)
            for token in stuff.split():
                if (match := block_re.match(token)) is not None:
                    ru = match.group(1)
                    bstart = match.group(2)
                    bend = match.group(3)
                    if ru == 'R':
                        r.addReachable(bstart, bend)
                    elif ru == 'U':
                        r.addUnreachable(bstart, bend)
                else:
                    r.addTrait(token)
            routines.append(r)
            debug(f"Found routine {r}")
        elif (match := segment_re.match(line)) is not None:
            name = match.group(1)
            type = match.group(2)
            addr = match.group(3)
            segments.append(Segment(name, type, addr))
    return routines, segments

def findMatch(needle, haystack):
    for candidate in haystack:
        if needle.match(candidate):
            return candidate
    return None

def main(map1_path, map2_path):
    debug(f"Opening map1: {map1_path}, map2: {map2_path}")
    r1, s1 = parseMap(map1_path)
    r2, s2 = parseMap(map2_path)
    info(f"Found {len(r1)} routines in {map1_path} and {len(r2)} in {map2_path}")
    perfect_count = 0
    rem = []
    for r in r1:
        debug(f"Searching for match of routine {r.name}")
        m = findMatch(r, r2)
        if m is not None:
            debug(f"Found perfect match for routine {r.name} with {m.name}")
            r2.remove(m)
            perfect_count += 1
        else:
            rem.append(r)
    r1 = rem
    rem = []
    for r in r2:
        debug(f"Searching for match of routine {r.name}")
        m = findMatch(r, r1)
        if m is not None:
            debug(f"Found perfect match for routine {r.name} with {m.name}")
            r1.remove(m)
            perfect_count += 1
        else:
            rem.append(r)
    r2 = rem
    info(f"Found perfect matches for {perfect_count} from xref")
    if r1:
        info(f"--- After search, {len(r1)} routines still have no match from {map1_path}")
        print(*r1, sep='\n')
    if r2:
        info(f"--- After search, {len(r2)} routines still have no match from {map2_path}")
        print(*r2, sep='\n')

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