#!/usr/bin/env python3
import json
import re
from output import *
from helper import Location, Extract, Block
from util import parseNum

class Config:
    COMMENT_RE = re.compile(r'^\s*//.*')
    def __init__(self, confpath):
        debug(f"Loading configuration from {confpath}")
        self.confpath = confpath
        self.preamble = []
        self.include = ""
        self.in_segments = []
        self.out_segments = []
        self.code_segments = []
        self.data_segments = []
        self.data_size = 0
        self.replace = []
        self.insert = []
        self.extract = []
        self.remove = []
        self.preserves = []
        self.externs = []
        self.publics = []
        self.header_preamble = ""
        self.header_coda = ""
        self.loadConfig()
        self.setConfig()
    def setConfig(self):
        for name in [ "preamble", "include", "in_segments", "code_segments", "data_segments", "preserves", "externs", "publics", "header_preamble", "header_coda" ]:
            if name in self.config:
                v = self.config[name]
                debug(f"{name}: {v}")
                setattr(self, name, v)
        if 'data_size' in self.config:
            v = parseNum(self.config['data_size'])
            debug(f"data_size: {v}")
            self.data_size = v
        for name in [ 'remove', 'replace', 'insert' ]:
            if name in self.config:
                for i in self.config[name]:
                    l = self.makeLocation(i)
                    debug(f"{name}: {l}")
                    getattr(self, name).append(l)
        if 'extract' in self.config:
            for e in self.config['extract']:
                v = self.makeExtract(e)
                debug(f"extract: {v}")
                self.extract.append(v)
    def loadConfig(self):
        file = open(self.confpath)
        json_str = ""
        real_lines = 0
        filtered_lines = 0
        linenos = {}
        for line in file:
            real_lines += 1
            # fuck you Douglas Crockford
            if self.COMMENT_RE.match(line):
                continue
            filtered_lines += 1
            # remember mapping between filtered and real line numbers
            linenos[filtered_lines] = real_lines
            json_str += line
            debug(f"{real_lines}/{filtered_lines}: {line}", end='')
        debug(f"Counted read lines: {real_lines}, after filtering: {filtered_lines}")
        try:
            self.config = json.loads(json_str)
        except json.JSONDecodeError as e:
            debug(f"exception on line {e.lineno}")
            real_lineno = linenos[e.lineno]
            error(f"JSON parsing error on line {real_lineno}: {e.msg}")

    def makeLocation(self, d):
        seg = d['seg'] if 'seg' in d else None
        off = d['off'] if 'off' in d else None
        contents = d['from'] if 'from' in d else None
        replace = d['to'] if 'to' in d else None
        return Location(seg, off, contents, replace)
    
    def makeExtract(self, d):
        seg = d['seg'] if 'seg' in d else None
        begin = parseNum(d['begin']) if 'begin' in d else 0
        end = parseNum(d['end']) if 'end' in d else 0
        match_begin = d['from'] if 'from' in d else None
        match_end = d['to'] if 'to' in d else None
        ported = d['ported'] if 'ported' in d else True
        return Extract(seg, Block(begin, end), match_begin, match_end, ported)

# this is an unused experiment for a superset of json, with comments, references and includes, doesn't work due to unsolved json tree walking and update
def parseConf():
    # filter out comment lines
    # TODO: support include
    comment_re = re.compile(r'^\s*#.*')
    file = open('conf/sample.json')
    json_str = ""
    real_lines = 0
    filtered_lines = 0
    linenos = {}
    for line in file:
        real_lines += 1
        if comment_re.match(line):
            continue
        filtered_lines += 1
        # remember mapping between filtered and real line numbers
        linenos[filtered_lines] = real_lines
        json_str += line
        debug(f"{real_lines}/{filtered_lines}: {line}", end='')

    debug(f"Counted read lines: {real_lines}, after filtering: {filtered_lines}")

    try:
        config = json.loads(json_str)
    except json.JSONDecodeError as e:
        debug(f"exception on line {e.lineno}")
        real_lineno = linenos[e.lineno]
        error(f"JSON parsing error on line {real_lineno}: {e.msg}")

    # path = []
    # stack = [config]
    # while stack:
    #     item = stack.pop() 
    #     if isinstance(item, dict):
            
    #     elif isinstance(item, list):
    #         stack.append(item)
