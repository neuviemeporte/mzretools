#!/usr/bin/env python3
import sys

DEBUG = False
def setDebug(arg):
    global DEBUG
    DEBUG = arg

def debug(str, end='\n'):
    if DEBUG:
        print(str, end=end)

def info(str):
    print(str)

def error(str):
    print(f"ERROR: {str}")
    sys.exit(1)