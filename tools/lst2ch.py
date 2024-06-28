#!/usr/bin/env python3
import sys
import os
from pathlib import Path
from output import error, debug, info, setDebug
from config import Config
from util import Regex, parseNum, joinStrings, sizeStr, ignoredProc, splitData, percentStr, tweakComment
from helper import Routine, Variable, Struct, Datatype, LstIterator, StructIterator, enforceType

# TODO: 
# struct N dup <P, Q, ...> 
# struct <0> when more than one member
# generate struct definitions

def parseData(valstr, structs):
    debug(f"Parsing value string: {valstr}")
    data = []
    size = 0
    valsplit = splitData(valstr)
    datatype = Datatype.UNK
    comment = None
    for i in valsplit:
        #debug(f"Parsing substring: {i}")
        if (match := Regex.DUPVAL.match(i)) is not None: # N dup(M) expression
            dupcount = parseNum(match.group(1))
            dupval = parseNum(match.group(2))
            if dupval is not None: # numerical repeat count
                duptype = Datatype.ARRAY
            else: # uninitialized data
                duptype = Datatype.BSS
            datatype = enforceType(datatype, duptype)
            data.extend([dupval] * dupcount)
            size += dupcount
        elif (match := Regex.OFFSET.match(i)) is not None: # offset <varName> expression
            datatype = enforceType(datatype, Datatype.PTR)
            offname = match.group(1)
            data.append(f"&{offname}")
            size += 1
        elif (match := Regex.SEGMENT.match(i)) is not None: # seg <segName> expression
            datatype = enforceType(datatype, Datatype.PTR)
            segname = match.group(1)
            data.append('NULL') # XXX: no idea how to deal with it for now
            comment = [f"segment {segname}"]
            size += 1
        elif i.startswith("'") and i.endswith("'"): # quoted string
            if len(valsplit) == 2 and valsplit[1].strip() == '0': # ASCIIZ
                datatype = enforceType(datatype, Datatype.CSTR)
                string = i.strip("'")
                size += len(string) + 1
                string = string.replace('"', '\\"')
                data.append(f'"{string}"')
            else: # non-ASCIIZ character array
                datatype = enforceType(datatype, Datatype.ARRAY)
                string = i.strip("'")
                size += len(string)
                data.extend(list(string))
        elif i == "?": # uninitialized data
            datatype = enforceType(datatype, Datatype.BSS)
            size += 1
            data.append(None)
        elif i == '0' and datatype == Datatype.CSTR: # ignore zero at end of cstring
            pass
        elif Regex.NUMBER.match(i): # single numeric value
            datatype = enforceType(datatype, Datatype.ARRAY)
            size += 1
            data.append(parseNum(i))
        elif (match := Regex.STRUCTBSSDUP.match(i)) is not None: # array of bss structs
            structname = match.group(1)
            count = parseNum(match.group(2))
            struct = getStruct(structs, structname)
            debug(f"Structure array, struct: {struct.name}, count: {sizeStr(count)}, itemsize: {sizeStr(struct.getSize())}")
            datatype = enforceType(datatype, Datatype.BSS)
            size += count
            # create fake values for every member in the struct times the number of items in the array
            data.extend(count * struct.getMemberCount() * [None])
        else:
            error(f"Unrecognized value string: {i}")
    debug(f"parsed data: len = {sizeStr(len(data))}, count = {sizeStr(size)}, type = {Datatype.NAME[datatype]}")
    return data, size, datatype, comment

def parseInc(path, config):
    structs = {}
    enums = {}
    with open(path, encoding="cp437") as incfile:
        for line in incfile:
            if (match := Regex.STRUCT.match(line)) is not None:
                structname = match.group(1)
                structsize = parseNum(match.group(2))
                debug(f'=== Found struct {structname}, size = {sizeStr(structsize)}')
                if not structsize > 0:
                    continue
                struct = Struct(structname, structsize)
                iter = StructIterator(incfile, structname, structsize)
                _, structvars, _, _ = parseListing(iter, config, {})
                if not structvars:
                    error("No structure variables could be identified")
                struct.addVars(structvars)
                structs[structname] = struct
            elif (match := Regex.ENUM.match(line)) is not None:
                enumname = match.group(1)
                enumval = parseNum(match.group(2))
                debug(f'=== Found enum {enumname} = {sizeStr(enumval)}')
                enums[enumname] = enumval
    return structs, enums

def sumData(count, var, total):
    if var:
        datasize = count * var.itemsz
    else:
        datasize = count
    total += datasize
    debug(f"Added {sizeStr(datasize)} bytes of data, total now {sizeStr(total)}")    
    return total

# TODO: yield
prev_offset = 0
def checkDataSize(size, offset):
    global prev_offset
    debug(f"Checking data size {sizeStr(size)} against offset {sizeStr(offset)} (prev = {sizeStr(prev_offset)})")
    if not offset or offset == prev_offset:
        return
    if size != offset:
        error(f"Summed up data size {sizeStr(size)} does not agree with offset {sizeStr(offset)}")
    prev_offset = offset

def saveVariable(var, vars, offset, total_size):
    '''store variable in list, add its size to running total'''
    if not var:
        return total_size
    
    # if var.name == 'stru_1892E':
    #     breakpoint()
    varsize = var.size()
    vars.append(var)
    total_size += varsize
    debug(f'Closed variable {var.name}, added {sizeStr(varsize)} to total variable size, now {sizeStr(total_size)}')
    # check only if the current offset is different from the var just closed - for example initialized struct arrays do not change the offset
    if offset and total_size != offset and offset != var.offset:
        error(f"Summed up variable size {sizeStr(total_size)} does not agree with offset {sizeStr(offset)}")
    return total_size

def getStruct(structs, name):
    if name in structs:
        return structs[name]
    error(f"Unable to find definition of struct {name}")

def parseListing(iter, config, structs):
    curVar = None
    curSub = None
    variables = []
    procs = []
    # two independent counters for verifying the data segment layout, should always match
    # sum of parsed data values
    total_datasize = 0
    # sum of parsed variable sizes
    total_varsize = 0
    proc_ignored = 0
    proto_str = ''
    proto_off = 0
    decl_str = ''
    decl_off = 0

    while True:
        # lstfile eof
        if (item := iter.nextItem()) is None:
            break
        segment, offset, instr, comment = item
        comment = [ tweakComment(comment) ]

        if segment and not segment in config.in_segments:
            continue

        # TODO: refactor, lots of repetition
        if segment in config.code_segments:
            if (match := Regex.PROC.match(instr)) is not None: # subroutine start
                procname = match.group(1)
                proctype = match.group(2)
                if ignoredProc(procname):
                    proc_ignored += 1
                    continue
                comment.append(f"==== {segment}:{hex(offset)} ====")
                if curSub:
                    error(f"Start of routine {procname} while {curSub.name} not closed")
                curSub = Routine(procname, proctype, offset, comment)
                if proto_str and proto_off == offset: # set explicit prototype if available
                    curSub.setPrototype(proto_str)
                debug(f"=== Opened routine {curSub.name}")
            elif (match := Regex.ENDP.match(instr)) is not None: # subroutine end
                procname = match.group(1)
                if ignoredProc(procname): # ignore C library routines
                    continue            
                if not curSub or curSub.name != procname:
                    error(f"End of routine {procname} while not open")
                curSub.close(offset)
                debug(f"=== Closed routine {curSub.name}, size = {sizeStr(curSub.size)}")
                procs.append(curSub)
                curSub = None
            elif not instr and comment[0]:
                if (match := Regex.PROTO.match(comment[0])) is not None: # subroutine prototype
                    proto_str = comment[0]
                    proto_off = offset
                    debug(f"Prototype: {proto_str}")
                elif (match := Regex.COLLAPSED.match(comment[0])) is not None: # collapsed function in code segment
                    procsize = int(match.group(1), 16)
                    procname = match.group(2)
                    if ignoredProc(procname):
                        proc_ignored += 1
                        continue
                    comment = [f"==== {segment}:{hex(offset)} ===="]
                    debug(f"Collapsed function '{procname}, size = {procsize}")
                    if curSub:
                        error(f"Collapsed function while routine {curSub.name} not closed")
                    curSub = Routine(procname, 'near', offset, comment)
                    curSub.close(offset + procsize)
                    procs.append(curSub)
                    curSub = None
            elif instr and not curSub and procs and procs[-1].name : # nop or db 0 outside of subroutine
                if instr == 'nop' or instr == 'db 0':
                    debug(f'Encountered code outside of subroutine at {hex(offset)}: {instr}')
                    # create fake "routine" to place module border comment in header file
                    procs.append(Routine(None, None, offset, None))
            else:
                debug("Code line ignored")
        elif segment in config.data_segments or not segment:
            checkDataSize(total_datasize, offset)
            if (match := Regex.SECRET.match(comment[0])): # special message to ourselves in ida comment
                msg = match.group(1)
                debug(f"Secret message: {msg}")
                if curVar and curVar.offset == offset:
                    curVar.setDecl(msg)
            if (match := Regex.VAR.match(instr)) is not None: # name + data
                dataname = match.group(1)
                itemsz = Datatype.TYPESIZE[match.group(2)]
                datavalue = match.group(3)
                debug(f"Named variable, name = '{dataname}', item size = '{itemsz}', value = '{datavalue}'")
                if curVar:
                    # have previous variable, need to close
                    total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                data, count, dtype, comm = parseData(datavalue, structs)
                if comm:
                    comment.append(comm)
                curVar = Variable(dataname, offset, itemsz, comment, decl_str, decl_off)
                if not curVar.addData(data, dtype, itemsz):
                    error("Data rejected by new variable?!")
                total_datasize = sumData(count, curVar, total_datasize)
            elif (match := Regex.DATA.match(instr)) is not None: # unnamed data
                itemtype = match.group(1)
                itemsz = Datatype.TYPESIZE[itemtype]
                datavalue = match.group(2)
                debug(f"Data continuation, item size = '{itemsz}', value = '{datavalue}'")
                data, count, dtype, comm = parseData(datavalue, structs)
                # TODO: make dummy also if current var is word, but data is bytes
                if not (curVar and curVar.addData(data, dtype, itemsz)): 
                    # data rejected by current variable, or no variable active, need to create a dummy new one
                    debug("Need to create dummy variable")
                    total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                    curVar = Variable(f"{segment}_{Datatype.CTYPE[itemtype]}_" + ("%x" % offset), offset, itemsz, comm, decl_str, decl_off)
                    curVar.setDummy(True)
                    if not curVar.addData(data, dtype, itemsz):
                        error("Data rejected by new dummy variable?!")
                total_datasize = sumData(count, curVar, total_datasize)
            elif not instr and comment[0]: # standalone comment in data segment
                if (match := Regex.COLLAPSED.match(comment[0])) is not None: # collapsed function in data segment
                    procsize = int(match.group(1), 16)
                    procname = match.group(2)
                    debug(f"Collapsed function '{procname}, size = {procsize}")
                    if curVar:
                        total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                    curVar = Variable(procname, offset, 1, None)
                    data = procsize * [0] # fake data contents since we don't know what's inside
                    if not curVar.addData(data, Datatype.ARRAY, 1):
                        error("Data rejected by new collapsed function variable?!")
                    total_datasize = sumData(procsize, None, total_datasize)
                else: # potential custom data declaration
                    decl_str = comment[0]
                    decl_off = offset
                    debug("Saved comment as potential declaration")
            elif (match := Regex.FARJMP.match(instr)) is not None: # far jump within data segment
                target = match.group(1)
                debug(f"far jump to {target}")
                total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                curVar = Variable(f"jmp_{target}", offset, 1, comment)
                data = [ 0xea, 0x00, 0x00, 0x00, 0x00] # far jmp 0:0
                if not curVar.addData(data, Datatype.ARRAY, 1):
                    error("Data rejected by new dummy jump variable?!")
                total_datasize = sumData(len(data), curVar, total_datasize)
            elif (match := Regex.STRUCTBSSVAR.match(instr)) is not None: # uninitialized structure
                varname = match.group(1)
                structname = match.group(2)
                debug(f"Structure bss variable, name: {varname}, struct: {structname}")
                struct = getStruct(structs, structname)
                # close previous variable if present
                total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                curVar = Variable(varname, offset, struct.getSize(), comment, decl_str, decl_off, struct)
                curVar.addData([None] * struct.getMemberCount(), Datatype.BSS, struct.getSize())
                total_datasize = sumData(1, curVar, total_datasize)
            elif (match := Regex.STRUCTBSSARR.match(instr)) is not None: # arrray of uninitialized structures
                varname = match.group(1)
                datavalue = match.group(2)
                structname = match.group(3)
                debug(f"Array of bss structures, name: {varname}, data: {datavalue}")
                data, count, dtype, comm = parseData(datavalue, structs)
                struct = getStruct(structs, structname)
                # close previous variable if present
                total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                curVar = Variable(varname, offset, struct.getSize(), comment, decl_str, decl_off, struct)
                curVar.addData(data, dtype, struct.getSize())
                total_datasize = sumData(count, curVar, total_datasize)
            elif (match := Regex.STRUCTVAR.match(instr)) is not None: # initialized structure var
                varname = match.group(1)
                structname = match.group(2)
                vardata = match.group(3)
                debug(f"Initialized structure variable, name: {varname}, structname: {structname}, data: {vardata}")
                total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                data, count, dtype, comm = parseData(vardata, structs)
                struct = getStruct(structs, structname)
                curVar = Variable(varname, offset, struct.getSize(), comment, decl_str, decl_off, struct)
                curVar.addData(data, dtype, struct.getSize(), struct.name)
                total_datasize = sumData(1, curVar, total_datasize)
            elif (match := Regex.STRUCTINIT.match(instr)) is not None: # unnamed initialized structure data
                structname = match.group(1)
                vardata = match.group(2)
                debug(f"Initialized structure data, structname: {structname}, data: {vardata}")
                struct = getStruct(structs, structname)
                data, count, dtype, comm = parseData(vardata, structs)
                if not (curVar and curVar.addData(data, dtype, struct.getSize())): 
                    # data rejected by current variable, or no variable active, need to create a dummy new one
                    debug("Need to create dummy variable")
                    total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                    curVar = Variable(f"{segment}_struc_" + ("%x" % offset), offset, struct.getSize(), comm, decl_str, decl_off, struct)
                    curVar.setDummy(True)
                    if not curVar.addData(data, dtype, struct.getSize(), struct.name):
                        error("Data rejected by new dummy variable?!")
                total_datasize = sumData(1, curVar, total_datasize)
            elif (match := Regex.STRUCTARR.match(instr)) is not None: # named initialized structure array
                varname = match.group(1)
                structname = match.group(2)
                dupval = parseNum(match.group(3))
                vardata = match.group(4)
                debug(f"Initialized structure array, var: {varname}, struct: {structname}, dup: {dupval}, data: {vardata}")
                total_varsize = saveVariable(curVar, variables, offset, total_varsize)
                struct = getStruct(structs, structname)
                curVar = Variable(varname, offset, struct.getSize(), comment, decl_str, decl_off, struct)
                data, count, dtype, comm = parseData(vardata, structs)
                # TODO: implement a better way of handling input by struct vars
                if len(data) != struct.getMemberCount():
                    if len(data) == 1 and data[0] == 0:
                        data = [0] * struct.getMemberCount() * dupval
                    else:
                        error(f"Invalid data size {sizeStr(len(data))} to initialize struct of type {structname}")
                curVar.addData(data, dtype, struct.getSize(), struct.name)
                total_datasize = sumData(dupval, curVar, total_datasize)
            else:
                debug("Data line ignored")
        else:
            debug("Unknown line ignored")
    
    if curVar:
        # TODO: force close variable when segment closes
        variables.append(curVar)
    if curSub:
        error(f"Unclosed procedure at EOF: {curSub.name}")

    return procs, variables, proc_ignored, total_datasize

def calculateExtracts(config, procs, proc_ignored, remaining_size):
    '''iterate over extract items to calculate size of completed (reconstructed) code'''
    ported_size = 0
    ported_routines = 0
    for e in config.extract:
        if not e.ported or not e.segment in config.code_segments:
            continue
        if e.end == 'endp':
            ported_routines += 1
        esize = e.block.end - e.block.begin
        ported_size += esize
    total_routines = len(procs) + proc_ignored
    remain_routines = total_routines - proc_ignored
    needport_routines = remain_routines - ported_routines
    ported_percent = percentStr(ported_size/remaining_size) if remaining_size != 0 else '0%'
    needport_size = remaining_size - ported_size
    needport_percent = percentStr(needport_size/remaining_size) if remaining_size else '100%'
    # TODO subtract or display number of extract routines
    info(f"Found routines: total: {total_routines}, ignored: {proc_ignored}, remaining: {remain_routines}, ported: {ported_routines}, unported: {needport_routines}")
    info(f"Accumulated routines' size: remaining: {sizeStr(remaining_size)}/100%, ported: {sizeStr(ported_size)}/{ported_percent}, unported: {sizeStr(needport_size)}/{needport_percent}")    

def writeEnums(enums, hfile):
    if not hfile:
        return
    for e,v in enums.items():
        hfile.write(f"#define {e} {hex(v)}\n");

def writeProcs(procs, hfile):
    remaining_size = 0
    for p in procs:
        remaining_size += p.size
        if hfile and p.name != 'main':
            hfile.write(p.toString() + "\n")
    return remaining_size

def writeVars(vars, cfile, hfile):
    '''write data definitions to .c file and declarations to .h file'''
    datasum_size = 0
    oldtype = Datatype.UNK
    for v in vars:
        datasum_size += v.size()
        debug(f"Processing variable {v.name}, data size {sizeStr(datasum_size)}")
        if cfile:
            if v.comment and len((commstr := " ".join(v.comment))) > 0:
                cfile.write("// " + commstr + "\n")
            if oldtype != Datatype.BSS and v.dtype == Datatype.BSS:
                cfile.write('// BSS values follow\n')
                cfile.write(v.defString() + "\n")
        if hfile and v.decl != 'ignore' and not v.dummy:
            hfile.write(v.declString() + "\n")
        oldtype = v.dtype
    return datasum_size

def main():
    argc = len(sys.argv)
    if argc < 4:
        error("Syntax: lst2asm infile outdir conffile [--noc] [--noh] [--debug]")

    lstpath = sys.argv[1]
    outdir = sys.argv[2]
    confpath = sys.argv[3]

    lstname, _ = os.path.splitext(os.path.basename(lstpath))
    output_c = True
    output_h = True
    hpath = f"{outdir}/{lstname}.h"
    cpath = f"{outdir}/{lstname}.c"
    for i in range(4, argc):
        argv = sys.argv[i]
        if argv == "--noc":
            output_c = False
        elif argv == "--noh":
            output_h = False
        elif argv == "--debug":
            setDebug(True)

    if not os.path.isfile(lstpath):
        error(f"Input file does not exist: {lstpath}")
    if not os.path.isfile(confpath):
        error(f"Config file does not exist: {confpath}")
    if not os.path.isdir(outdir):
        error(f"Invalid output directory : {outdir}")

    # import settings from conffile
    config = Config(confpath)

    # discover struct and enum definitions from incfile if it exists
    structs = {}
    enums = {}
    incpath = os.path.splitext(lstpath)[0] + '.inc'
    if os.path.isfile(incpath):
        info(f"Found matching include file: {incpath}, parsing")
        structs, enums = parseInc(incpath, config)
        debug(f"Done parsing include file, registered {len(structs)} structs, {len(enums)} enums")
    
    # discover functions and data from lst file
    debug(f"Parsing listing file")
    lstfile = open(lstpath, 'r', encoding='cp437')
    lst_iter = LstIterator(lstfile)
    procs, vars, proc_ignored, total_data = parseListing(lst_iter, config, structs)
    # don't continue if input file was empty
    if len(procs) == 0 and len(vars) == 0:
        info("Input file empty, no action")
        # touch files for make
        if output_h:
            Path(hpath).touch()
        if output_c:
            Path(cpath).touch()
        sys.exit(0)

    hfile = None
    if output_h:
        info("Writing C header file: " + hpath)
        hfile = open(hpath, "w", encoding='cp437')
        if config.header_preamble:
            hfile.write(joinStrings(config.header_preamble))
    cfile = None
    if output_c:
        info("Writing C source file: " + cpath)
        cfile = open(cpath, "w", encoding='cp437')

    # write out discovered items to header and c file
    writeEnums(enums, hfile)
    remaining_size = writeProcs(procs, hfile)
    calculateExtracts(config, procs, proc_ignored, remaining_size)
    datasum_size = writeVars(vars, cfile, hfile)

    if output_h and config.header_coda:
        hfile.write(joinStrings(config.header_coda))
    info(f"Found {len(vars)} variables, size = {sizeStr(datasum_size)}")
    if datasum_size != total_data:
        error(f"Accumulated data size ({sizeStr(datasum_size)}) different than running total {sizeStr(total_data)}")
    if datasum_size != config.data_size:
        error(f"Accumulated data size ({sizeStr(datasum_size)}) different than expected {sizeStr(config.data_size)}")    

if __name__ == '__main__':
    main()