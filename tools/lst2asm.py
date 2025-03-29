#!/usr/bin/env python3
#
# Script for converting IDA listing (.lst) files into assembly files that can be built with UASM.
# Also applies some corrections which allows it to generate a 100% identical binary image.
#
import sys
import re
import os.path
from pathlib import Path
import traceback

from output import debug, info, error, setDebug
from helper import LstIterator, Output
from config import Config
from util import findExtract, tweakComment, splitInstr, contains, isLabel, isData, Regex

def openExtracts(items, dir):
    for i in items:
        if not i.filename:
            continue
        f = open(f"{dir}/{i.filename}", "w")
        f.write(preamble)
        # generate segment forward declarations
        for s in out_segments:
            segname, segclass = s
            if segname == i.segment:
                continue
            f.write(f"{segname} segment byte public '{segclass}'\n")
            f.write(f"{segname} ends\n\n")
        f.write(f"{i.segment} segment byte public 'CODE'\n")

def closeExtracts(items, dir):
    for i in items:
        if not i.filename:
            continue
        path = f"{dir}/{i.filename}"
        if not os.path.isfile(path):
            continue
        f = open(path, "a")
        f.write(f"{i.segment} ends\nend\n")

def main(lstpath, asmpath, confpath):
    if len(sys.argv) < 4:
        error("Syntax: lst2asm infile outfile conffile [--debug] [--stub] [--noproc] [--nopreserve]")

    opcode_map = { 'add': '05h', 'cmp': '3Dh', 'and': '25h', 'sub': '2Dh', 'sbb': '1Dh' }

    # routine processing modes:
    # 1. all routines copied verbatim (default)
    # 2. routines replaced with stubs for comparison (--stub)
    # 3. no routines copied, just data remains (--noproc)
    # 4. preserves present in config: keep these despite --noproc
    # 5. preserves disabled (--nopreserve)
    stub = False
    noProc = False
    noPreserve = False

    for i in range(4, len(sys.argv)):
        argv = sys.argv[i]
        if argv == "--debug":
            setDebug(True)
        elif argv == '--stub':
            stub = True
        elif argv == '--noproc':
            noProc = True
        elif argv == '--nopreserve':
            noPreserve = True
        else:
            error(f"Unsupported option: {argv}")

    if not os.path.isfile(lstpath):
        error(f"Input file does not exist: {lstpath}")
    if not os.path.isfile(confpath):
        error(f"Config file does not exist: {confpath}")
    lst_stat = os.stat(lstpath)
    if lst_stat.st_size == 0:
        info("Input file empty, no action")
        # touch output file for make
        Path(asmpath).touch()
        sys.exit(0)

    asmdir = os.path.dirname(asmpath)
    if not os.path.isdir(asmdir):
        asmdir = '.'

    # import settings from conffile
    config = Config(confpath)

    lstfile = open(lstpath, 'r', encoding='cp437')
    asmfile = open(asmpath, 'w', encoding='cp437')
    openExtracts(config.extract, asmdir)

    # write out preamble + all externs and publics first
    for p in config.preamble:
        asmfile.write(p + "\n")
    for e in config.externs:
        asmfile.write(f'EXTRN _{e}:PROC\n')
    for p in config.publics:
        # only write out publics for symbols not in preserves, or if preserves are... preserved
        if p not in config.preserves or not noPreserve:
            asmfile.write(f'PUBLIC _{p}\n')
    # process include file if present
    if config.include:
        if not os.path.isfile(config.include):
            error(f"Include file does not exist: {config.include}")
        with open(config.include) as i:
            idata = i.read()
            asmfile.write(idata)

    cur_extract = None
    cur_proc = None
    output = Output(asmfile)
    lst_iter = LstIterator(lstfile)
    output_procs = []

    while True:
        # lstfile eof
        if (item := lst_iter.nextItem()) is None:
            break 
        segment, offset, instr, comment = item
        writeList = [ instr ]
        skipWrite = False
        actionMatch = False

        # ignore unknown segments
        if segment not in config.in_segments:
            continue

        # ignore lines with no instruction
        if not instr:
            continue

        # handle code preservation and elimination
        if segment in config.code_segments:
            # routine start
            if (match := Regex.PROC.match(instr)) is not None: # procedure start
                cur_proc = match.group(1)
                if noProc and (noPreserve or cur_proc not in config.preserves):
                    debug(f"Ignoring start of non-preserved routine {cur_proc}")
                    skipWrite = True
            # routine end
            elif (match := Regex.ENDP.match(instr)) is not None: # procedure end
                endproc = match.group(1)
                if not cur_proc:
                    error(f"End of routine {endproc} while not inside a routine?")
                elif endproc != cur_proc:
                    error(f"End of routine {endproc} while inside routine {cur_proc}?")
                if noProc and (noPreserve or cur_proc not in config.preserves):
                    skipWrite = True                
                # prepend near return to write list on procedure end for stubs
                elif stub and (noPreserve or cur_proc not in config.preserves): 
                    writeList.insert(0, 'retn')
                # gather names of all procs written to output
                output_procs.append(cur_proc)
                cur_proc = None
            # some instructions outside a procedure
            elif not cur_proc: 
                if noProc or not (contains(instr, "segment") or contains(instr, "ends") or isData(instr)):
                    debug(f"Ignoring instructions outside routine")
                    skipWrite = True
            # instructions inside a procedure
            elif stub and (noPreserve or cur_proc not in config.preserves):
                debug(f"Ignoring instructions in stubbed routine {cur_proc}")
                skipWrite = True
            elif noProc and (noPreserve or cur_proc not in config.preserves):
                debug(f"Ignoring instructions of non-preserved routine {cur_proc}")
                skipWrite = True

        # open new extraction sink if segment:offset and optionally current line matches an entry
        if cur_extract is None:
            cur_extract = findExtract(segment, offset, instr, config.extract)
            if cur_extract:
                msg = f"{segment}:{hex(offset)}: starting extraction on '{instr}', range {str(cur_extract.block)} to "
                # open file for append if sink has a filename
                if cur_extract.filename:
                    cur_extract.handle = open(f"{asmdir}/{cur_extract.filename}", 'a')
                    output.handle = cur_extract.handle
                    msg += output.handle.name
                # otherwise output goes nowhere
                else:
                    msg += "nowhere"
                    output.handle = None
                debug(msg)

        comment = tweakComment(comment)

        # handle removals
        for r in config.remove:
            if r.match(segment, offset, instr):
                debug('Matched location to remove')
                skipWrite = True
                actionMatch = True
                break
        # handle replacements
        for r in config.replace:
            if r.match(segment, offset, instr):
                if actionMatch: 
                    error(f"Matched location {segment}:{offset} to replace while previously marked for remove")
                comment += instr
                writeList = r.getReplace()
                debug(f'Matched location to replace with: {writeList}')
                actionMatch = True
                break
        # handle insertions
        for i in config.insert:
            if i.match(segment, offset, instr):
                if actionMatch: 
                    error(f"Matched location {segment}:{offset} to insert while previously marked for remove or replace")
                insertions = i.getReplace()
                writeList.extend(insertions)
                debug(f'Matched location to insert: {insertions}')
                actionMatch = True
                break

        # prefix underscore in public and external symbol names as they need to be visible to/from C
        if not skipWrite:
            split = splitInstr(instr)
            splitIdx = 0
            splitLen = len(split)
            debug(f"Instruction exploded into {splitLen} pieces: {split}")
            for o in split:
                for p in config.publics + config.externs:
                    if o == p and (splitIdx + 1 == splitLen or split[splitIdx + 1] != '='): # do not prepend underscore in equates
                        writeList = [ writeList[i].replace(o, f"_{p}") for i in range(len(writeList)) ]
                        debug(f"Fixed public/extern declaration: {writeList}")
                splitIdx += 1

            # more complex substitutions
            # TODO: make more generic, configurable from conffile
            if (match := Regex.ALIGN.match(instr)) is not None:
                align = int(match.group(1), 16)
                count = align - (offset % align) 
                comment += instr
                if segment in config.code_segments:
                    writeList = [ 'nop' ] * count
                elif segment in config.data_segments:
                    if config.inBss(segment, offset):
                        writeList = [ 'db ?' ] * count
                    else:
                        writeList = [ 'db 0' ] * count
            elif (match := Regex.INSTR_AX.match(instr)) is not None:
                comment += instr
                opcode = opcode_map[match.group(1)]
                op2 = match.group(2)
                writeList = [ f'db {opcode}', f'dw {op2}' ]
            elif instr == 'jmp far ptr 0:0' or Regex.JMPSLOT.match(instr):
                comment += instr
                writeList = [ 'db 0EAh', 'dd 0' ]
            elif instr == 'repne movsw':
                comment += instr
                writeList = [ 'dw 0A5F2h' ]
            elif instr == 'or di, 2':
                comment += instr
                writeList = [ 'dd 2CF81h' ]

            # write actual instruction/directive
            firstInstr = True
            for w in writeList:
                indent = True
                # pre-demarcation and indentation
                if contains(w, 'segment') or contains(w, '.CODE') or contains(w, '.DATA') or contains(w, '.STACK'):
                    output.write('; ' + 78 * '=' + '\n')
                    indent = False
                elif contains(w, 'proc'):
                    output.write('; ' + 30 * '-' + segment + ":" + hex(offset) + 30 * '-' + '\n')
                    indent = False
                elif isData(w) or contains(w, 'ends') or contains(w, 'endp') or isLabel(w):
                    indent = False
                if indent:
                    output.write(4 * ' ')
                # write comment on the first instruction in the sequence
                if firstInstr and comment:
                    w += f" ;{comment}"
                output.write(w + '\n')
                firstInstr = False
                # post-demarcation
                if contains(w, 'ends'):
                    output.write('; ' + 78 * '=' + '\n')
                elif contains(w, 'endp'):
                    output.write('; ' + 30 * '-' + segment + ":" + hex(offset) + 30 * '-' + '\n')

        # close previous extraction sink
        if cur_extract and ((offset == cur_extract.block.end and cur_extract.matchEnd(instr)) or offset > cur_extract.block.end):
            if cur_extract.segment != segment:
                raise RuntimeError('Unexpected segment for extract')
            if cur_extract.handle:
                cur_extract.handle.close()
            cur_extract = None
            # restore default output file as destination
            output.handle = asmfile
            debug(f"{segment}:{hex(offset)}: ending extraction on '{instr}'")
        
    closeExtracts(config.extract, asmdir)

    for p in config.coda:
        asmfile.write(p + "\n")

    if asmfile.tell() == 0:
        error("Output file is empty, broken config?")

    # check if all preserved routines were written out to output file
    if config.preserves and not noPreserve:
        for p in config.preserves:
            if p not in output_procs:
                info(f"WARNING: Preserved routine not written to output: {p}")

if __name__ == '__main__':
    lstpath = sys.argv[1]
    asmpath = sys.argv[2]
    confpath = sys.argv[3]
    success = True
    try:
        main(lstpath, asmpath, confpath)
    except RuntimeError:
        success = False
    except:
        success = False
        traceback.print_exc()
    finally:
        if not success:
            out_path = Path(asmpath)
            if out_path.is_file():
                debug(f"Removing output file {asmpath} due to fatal error")
                out_path.unlink()
            sys.exit(1)