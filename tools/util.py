#!/usr/bin/env python3

import re

class Regex:
    NAME = '[_a-zA-Z0-9]+'
    DATA = 'db|dw|dd'
    NUMVAL = '[x0-9a-fA-F]+h?'
    CTYPE = 'char|int|void|__int32'
    SPACES = r'\s+'
    SPACEOPT = r'\s*'
    ALIGN = re.compile('^align ([0-9]+)')
    JMPSLOT = re.compile('^jmp.*(slot|misc)_')
    INSTR_AX = re.compile('^(cmp|add|and|sub|sbb) ax, ([_a-fA-F0-9]{1,5}h?)$')    
    PROC = re.compile(f'^({NAME}){SPACES}proc{SPACES}(near|far)')
    ENDP = re.compile(f'^({NAME}){SPACES}endp')
    VAR = re.compile(f'^({NAME}){SPACES}({DATA}){SPACES}(.*)')
    DATA = re.compile(f'^({DATA}){SPACES}(.*)')
    DUPVAL = re.compile(f'({NUMVAL}){SPACES}dup' + r'\(\s*' + f'({NUMVAL}' + r'|\?)\s*\)')
    OFFSET = re.compile(f'^offset{SPACES}({NAME})')
    SEGMENT = re.compile(f'^seg{SPACES}({NAME})')
    NUMBER = re.compile(f'^{NUMVAL}$')
    FARJMP = re.compile(f'jmp{SPACES}(?:far{SPACES})?(?:ptr{SPACES})?({NAME})')
    PROTO = re.compile(f'^(?:{CTYPE})' + r'\s*\*?\s*' + '(?:__cdecl)?' + r'\s*' + f'({NAME})' + r'\(.*\)')
    COLLAPSED = re.compile(r'\[' + f'({NUMVAL}) BYTES: COLLAPSED FUNCTION ({NAME}).*')
    SECRET = re.compile(f'^lst2ch:(.*)')
    STRUCT = re.compile(f'^({NAME}){SPACES}struc' + r'\s*;\s*\(\s*' +f'sizeof=({NUMVAL})')
    # <n[, n]...>
    STRUCTDATA = re.compile(f'<(({NUMVAL},?{SPACEOPT})+)>')
    # StructType <n[, n]...>
    STRUCTINIT = re.compile(f'({NAME}){SPACES}' + STRUCTDATA.pattern)
    # varName StructType <n[,n]...>
    STRUCTVAR = re.compile(f"^({NAME}){SPACES}" + STRUCTINIT.pattern)
    # StructType n dup(<n[,n]...>)
    STRUCTDUP = re.compile(f"({NAME}){SPACES}({NUMVAL}){SPACES}" + r'dup\(' + STRUCTDATA.pattern + r'\)')
    # varName StructType n dup(<n[,n]...>)
    STRUCTARR = re.compile(f"^({NAME}){SPACES}" + STRUCTDUP.pattern)
    # varName StructType <?>
    STRUCTBSSVAR = re.compile(f'^({NAME}){SPACES}({NAME}){SPACES}' + r'<\?>')
    # StructType n dup(<?>)
    STRUCTBSSDUP = re.compile(f'({NAME}){SPACES}({NUMVAL}){SPACES}dup' + r'\(<\?>\)')
    # varName StructType n dup(<?>)
    STRUCTBSSARR = re.compile(f'({NAME}){SPACES}' + f'({STRUCTBSSDUP.pattern})')
    ENUM = re.compile(f'({NAME}){SPACEOPT}={SPACEOPT}({NUMVAL})')    

def parseNum(string):
    if string == '?':
        return None
    if string.startswith('0x') or string.endswith('h'):
        return int(string.rstrip('h'), 16)
    else:
        return int(string)
    
def joinStrings(s):
    ret = ''
    if isinstance(s, list):
        for i in s:
            ret += i + "\n"
        return ret
    else:
        return s + "\n"
    
def sizeStr(value):
    if not value:
        return "None"
    return f"{value}/{hex(value)}"

def percentStr(ratio):
    return f"{round(ratio*100, 2)}%"

def ignoredProc(name):
    if name == 'start' or name.startswith('_'): # ignore startup code and C library routines
        return True
    return False

def squeeze(str):
    prevChar = None
    inQuote = False
    out = ''
    for c in str:
        if c == '\t':
            c = ' '
        if c == ' ':
            if not inQuote and prevChar == ' ':
                continue
        elif c == "'":
            inQuote = not inQuote
        out += c
        prevChar = c
    return out

def splitComment(str):
    inQuote = False
    inComment = False
    instr = ''
    comment = ''
    for c in str:
        if inComment:
            comment += c
            continue
        if c == '\'':
            inQuote = not inQuote
        elif c == ';' and not inQuote:
            inComment = True
            continue
        instr += c
    return instr, comment

def cstrlen(str):
    ret = 0
    if str.startswith('"') and str.endswith('"'):
        ret = -2
    for c in str:
        if c == '\\':
            continue
        ret += 1
    return ret + 1

def splitData(valstr):
    ret = []
    inQuote = False
    start = 0
    cur = 0
    for c in valstr:
        if c == "," and not inQuote:
            ret.append(valstr[start:cur].strip())
            cur += 1
            start = cur
            continue
        elif c == "'":
            inQuote = not inQuote
        cur += 1
    ret.append(valstr[start:cur].strip())
    return ret

def findExtract(segment, offset, instr, extract):
    for e in extract:
        if e.segment == segment and e.block.begin == offset and e.matchStart(instr):
            return e
    return None

def contains(instr, tmpl):
    return instr.find(tmpl) >= 0

def isData(instr):
    if contains(instr, ' db ') or contains(instr, ' dw ') or contains(instr, ' dd '):
        return True
    return False

def isLabel(instr):
    return instr.endswith(':')

def splitInstr(str):
    ret = []
    item = ''
    for s in str:
        if s in [' ', ',', '+', '[', ']', '(', ')', '.' ]:
            item = item.strip()
            if item:
                ret.append(item)
                item = ''
            continue
        item += s
    item = item.strip()
    if item:
        ret.append(item)
    return ret

def tweakComment(comment):
    # suppress useless comments
    if comment and (len(comment) <= 3 or contains(comment, 'XREF') or comment.startswith('\'')):
        comment = ''
    return comment

