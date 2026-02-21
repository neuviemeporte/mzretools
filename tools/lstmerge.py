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

def syntax():
    print('Syntax: lstmerge.py c_file lst_file out_file')
    sys.exit(1)

def debug(msg, end='\n'):
    print(msg, end=end)

def error(msg):
    print(f"ERROR: {msg}")
    sys.exit(1)


# load and parse disassembly listing file, store code lines of all procedures
def load_lst(lst_path):
    debug(f"=== Loading procs from lstfile {lst_path}")
    ADDR_RE = re.compile(r'([a-zA-Z0-9]+):([0-9a-fA-F]+)(\s+)(.*)')
    PROC_RE = re.compile(r'([_0-9a-zA-Z]+)\s+proc\s+(?:near|far)')
    ENDP_RE = re.compile(r'([_0-9a-zA-Z]+)\s+endp')
    SPACE_RE = re.compile(r'\s+')
    lst_file = open(lst_path, 'r')
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
            procname = match.group(1)
            debug(f"--- proc {procname}")
            if cur_proc is not None:
                error(f"proc {cur_proc} not ended before proc {procname}")
            cur_proc = procname
            procs[cur_proc] = [(segname, offset, indent, codestr)]
        elif (match := ENDP_RE.match(codestr)) is not None:
            procname = match.group(1)
            debug(f"--- endp {procname}")
            if cur_proc is None:
                error(f"proc {cur_proc} not ended before endp {procname}")
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
        error(f"Unable to find routine {cur_routine} in lst procs")
    # dump disassembly first in a C comment
    out_file.write('/*\n')
    while proc:
        (segname, offset, indent, codestr) = proc[0]
        if stop_offset > 0 and offset > stop_offset:
            break
        proc.pop(0)
        indent_str = " "
        if indent > 1:
            indent_str = "    "
        lst_line = f"{segname}:{offset:04X}{indent_str}{codestr}\n"
        out_file.write(lst_line)
        debug(lst_line, '')
    out_file.write('*/\n')
    # followed by C code
    while routine_lines:
        out_file.write(routine_lines[0])
        debug(routine_lines[0], '')
        routine_lines.pop(0)

# main processing: parse the disassembly listing, then go over the lines of the C code file, writing out the code to the output file, interleaved with corresponding disassembly in comments
def process(c_file, lst_path, out_file):
    ROUTINE_RE = re.compile(r'(void|int)\s([_a-zA-Z0-9]+)\(.*\)')
    OFFSET_RE = re.compile(r'(.*)//\s*(?:0x)?([0-9a-fA-F]+)$')
    COMMENT_RE = re.compile(r'\s*//')
    lst_procs = load_lst(lst_path)
    lineno = 0
    depth = 0
    cur_routine = None
    routine_lines = []
    stop_offset = None
    debug(f"=== Processing C file {c_file.name} to {out_file.name}")
    for line in c_file:
        lineno += 1
        depth_delta = line.count('{') - line.count('}')
        if depth + depth_delta < 0:
            error(f"Invalid bracing (depth = {depth}, delta {depth_delta}) on line {lineno}")
        # routine end - bracing level goes to zero while in a routine
        if cur_routine and depth > 0 and depth + depth_delta == 0:
            debug(f"{lineno} closing routine {cur_routine}")
            routine_lines.append(line)
            # write out any remaining disassembly and C code 
            write_lines(out_file, cur_routine, lst_procs, routine_lines)
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
            else:
                debug(f"Ignoring routine not found in lstfile")
        # offset mark comment within routine
        elif cur_routine and (match := OFFSET_RE.match(line)) is not None:
            # previous meaningful line was an offset too, dump the lines leading up to it
            if stop_offset:
                write_lines(out_file, cur_routine, lst_procs, routine_lines, stop_offset)
            # store any C code on the offset line, if present
            lead = match.group(1).rstrip()
            stop_offset = int(match.group(2), 16)
            routine_lines.append(line)
            debug(f"{lineno}: found offset: 0x{stop_offset:x}")
            # if there is code on the offset line, dump listing and code up to and including the line. Otherwise, delay the dump until the next line
            if lead and not lead.isspace():
                write_lines(out_file, cur_routine, lst_procs, routine_lines, stop_offset - 1)
                stop_offset = None
        # ignore comment lines
        elif (match := COMMENT_RE.match(line)) is not None:
            continue
        # other code line inside a routine
        elif cur_routine:
            routine_lines.append(line)
            # offset mark found before this line; dump listing and C code including this line
            if stop_offset:
                write_lines(out_file, cur_routine, lst_procs, routine_lines, stop_offset)
                stop_offset = None

def main():
    if len(sys.argv) < 4:
        syntax()
    c_path = sys.argv[1]
    lst_path = sys.argv[2]
    out_path = sys.argv[3]
    if not os.path.isfile(c_path):
        print(f'Error: {c_path} does not exist!')
        sys.exit(1)
    if not os.path.isfile(lst_path):
        print(f'Error: {lst_path} does not exist!')
        sys.exit(1)
    try:
        with open(c_path, 'r') as c_file, open(out_path, 'w') as out_file:
            process(c_file, lst_path, out_file)
    except:
        # e = sys.exc_info()
        # print('exception', e)
        traceback.print_exc()
        print(traceback.format_exc())
        sys.exit(1)

if __name__ == '__main__':
    main()