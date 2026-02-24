#!/usr/bin/env python3
#
# This script merges a C source file containing offset annotation comments
# with its equivalent assembly listing to create a C source file
# interspersed with assembly in comments.
# This is useful for cross-referencing C code with assembly in already
# reconstructed routines for debugging, and it can be used to supplement
# LLM knowledge with RAG.
#
import sys
import os.path
import re
import traceback
from output import debug, info, warn, error, setDebug

def syntax():
    print('Syntax: lstmerge.py c_file lst_file out_file')
    sys.exit(1)


# load and parse disassembly listing file, store code lines of all procedures
def load_lst(lst_path):
    debug(f"=== Loading procs from lstfile {lst_path}")
    ADDR_RE = re.compile(r'([a-zA-Z0-9]+):([0-9a-fA-F]+)(\s+)(.*)')
    PROC_RE = re.compile(r'([_0-9a-zA-Z]+)\s+proc\s+(?:near|far)')
    ENDP_RE = re.compile(r'([_0-9a-zA-Z]+)\s+endp')
    SPACE_RE = re.compile(r'\s+')
    lst_file = open(lst_path, 'r', encoding='cp437')
    lineno = 0
    cur_proc = None
    procs = {}
    for line in lst_file:
        lineno += 1
        addr_match = ADDR_RE.match(line)
        if addr_match is None:
            continue
        segname = addr_match.group(1)
        offset = int(addr_match.group(2), 16)
        indent = len(addr_match.group(3))
        codestr = SPACE_RE.sub(' ', addr_match.group(4).split(';')[0].rstrip())
        if len(codestr) < 1 or codestr[0] == ';':
            continue
        debug(f"{lineno}: seg {segname} off 0x{offset:X} code '{codestr}'")
        if (match := PROC_RE.match(codestr)) is not None:
            procname = match.group(1).lstrip('_')
            debug(f"--- proc {procname}")
            if cur_proc is not None:
                error(f"proc {cur_proc} not ended before proc {procname}")
            cur_proc = procname
            procs[cur_proc] = [(segname, offset, indent, codestr)]
        elif (match := ENDP_RE.match(codestr)) is not None:
            procname = match.group(1).lstrip('_')
            debug(f"--- endp {procname}")
            if cur_proc is None:
                error(f"proc {procname} ended outside of any proc")
            if procname != cur_proc:
                error(f"proc {procname} ended while in proc {cur_proc}")
            procs[cur_proc].append((segname, offset, indent, codestr))
            cur_proc = None
        elif cur_proc: 
            procs[cur_proc].append((segname, offset, indent, codestr))
    debug(f"lst loading complete, found {len(procs.keys())} procs")
    return procs

# write assembly and C code lines to output file
def write_lines(out_file, cur_routine, lst_procs, routine_lines, stop_offset=-1):
    #breakpoint()
    proc = lst_procs[cur_routine]
    if not proc:
        error(f"Ran out of listing lines before end of routine {cur_routine}")
    # dump disassembly first in a C comment
    out_lines = 0
    did_comment = False
    while proc:
        (segname, offset, indent, codestr) = proc[0]
        if stop_offset > 0 and offset > stop_offset:
            break
        proc.pop(0)
        indent_str = " "
        if indent > 1:
            indent_str = "    "
        lst_line = f"{segname}:{offset:04X}{indent_str}{codestr}\n"
        # only write comment heading if needed
        if not did_comment:
            out_file.write('/*\n')
            out_lines += 1
            did_comment = True
        out_file.write(lst_line)
        out_lines += 1
        debug(lst_line, '')
    if did_comment:
        out_file.write('*/\n')
        out_lines += 1
    # followed by C code
    while routine_lines:
        out_file.write(routine_lines[0])
        out_lines += 1
        debug(routine_lines[0], '')
        routine_lines.pop(0)
    return out_lines

# main processing: parse the disassembly listing, then go over the lines of the C code file, writing out the code to the output file, interleaved with corresponding disassembly in comments
def process(c_file, lst_path, out_path):
    ROUTINE_RE = re.compile(r'(void|int|uint32)(?:\*)?\s([_a-zA-Z0-9]+)\(.*\)')
    OFFSET1_RE = re.compile(r'(.*)//\s*(?:0x)?([0-9a-fA-F]+)$')
    OFFSET2_RE = re.compile(r'(.*)/\*\s*(?:0x)?([0-9a-fA-F]+)\s*\*/')
    COMMENT_RE = re.compile(r'\s*//')
    TRACE_RE   = re.compile(r'\s*TRACE')
    lst_procs = load_lst(lst_path)
    out_file = open(out_path, "w")
    lineno = 0
    depth = 0
    cur_routine = None
    routine_lines = []
    stop_offset = None
    out_lines = 0
    debug(f"=== Processing C file {c_file.name} to {out_file.name}")
    for line in c_file:
        lineno += 1
        debug(f"{lineno}: '{line.rstrip()}'")
        depth_delta = line.count('{') - line.count('}')
        if depth + depth_delta < 0:
            error(f"Invalid bracing (depth = {depth}, delta {depth_delta}) on line {lineno}")
        # routine end - bracing level goes to zero while in a routine
        if cur_routine and depth > 0 and depth + depth_delta == 0:
            debug(f"{lineno} closing routine {cur_routine}")
            routine_lines.append(line)
            # write out any remaining disassembly and C code 
            debug(f"Dumping lines due to routine end: {cur_routine}")
            out_lines += write_lines(out_file, cur_routine, lst_procs, routine_lines)
            out_file.write('\n')
            cur_routine = None
        depth += depth_delta
        # routine start
        if (match := ROUTINE_RE.match(line)) is not None:
            name = match.group(2)
            debug(f"{lineno}: found routine: {name}")
            if name in lst_procs:
                cur_routine = name
                routine_lines.clear()
                routine_lines.append(line)
                stop_offset = None
            else:
                warn(f"Ignoring routine not found in lstfile: {name}")
        # offset mark comment within routine
        elif cur_routine and ((match := OFFSET1_RE.match(line)) is not None or (match := OFFSET2_RE.match(line)) is not None):
            # store any C code on the offset line, if present
            lead = match.group(1).rstrip()
            stop_offset = int(match.group(2), 16)
            routine_lines.append(line)
            debug(f"{lineno}: found offset: 0x{stop_offset:x}")
            # if there is code on the offset line, dump listing and code up to and including the line. Otherwise, delay the dump until the next line
            if lead and not lead.isspace():
                debug(f"Dumping lines due to code present on offset line")
                out_lines += write_lines(out_file, cur_routine, lst_procs, routine_lines, stop_offset - 1)
                stop_offset = None
        # ignore comment and trace lines
        elif COMMENT_RE.match(line) or TRACE_RE.match(line):
            continue
        # other code line inside a routine
        elif cur_routine:
            routine_lines.append(line)
            # offset mark found before this line; dump listing and C code including this line
            if stop_offset:
                debug(f"Dumping lines due to stop offset being set: 0x{stop_offset:x} while processing routine line")
                out_lines += write_lines(out_file, cur_routine, lst_procs, routine_lines, stop_offset)
                stop_offset = None
    out_file.close()
    if out_lines == 0:
        debug(f"Empty output, removing {out_path}")
        os.remove(out_path)

def main():
    if len(sys.argv) < 4:
        syntax()
    c_path = None
    lst_path = None
    out_path = None
    for arg in sys.argv[1:]:
        if arg == '--debug':
            setDebug(True)
        elif not c_path:
            c_path = arg
        elif not lst_path:
            lst_path = arg
        elif not out_path:
            out_path = arg
        else:
            error(f'Unrecognized argument: {arg}')
    if not os.path.isfile(c_path):
        print(f'Error: {c_path} does not exist!')
        sys.exit(1)
    if not os.path.isfile(lst_path):
        print(f'Error: {lst_path} does not exist!')
        sys.exit(1)
    try:
        with open(c_path, 'r') as c_file:
            process(c_file, lst_path, out_path)
    except:
        # e = sys.exc_info()
        # print('exception', e)
        traceback.print_exc()
        print(traceback.format_exc())
        sys.exit(1)

if __name__ == '__main__':
    main()