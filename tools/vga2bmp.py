#!/usr/bin/env python3
from PIL import Image
import sys
import os.path
import re
from output import debug, setDebug, info

setDebug(True)
IMSIZE = (320,200)
IMLEN = IMSIZE[0] * IMSIZE[1]

argc = len(sys.argv)
if argc < 2 or argc > 4:
    print("Syntax: vga2bmp infile [palette] [WxH] [outfile]")
    sys.exit(1)

inpath = sys.argv[1]
if not os.path.isfile(inpath):
    print(f"Input file {inpath} does not exist!")
    sys.exit(1)

indir = os.path.dirname(inpath)
inname, inext = os.path.splitext(os.path.basename(inpath))
outpath = ''
if indir:
    outpath = indir + os.path.sep
outpath += inname + ".bmp"
if argc > 4:
    outpath = sys.argv[4]

infile = open(inpath, 'rb')
indata = infile.read()
insize = len(indata)
info(f"Reading VGA buffer contents from {inpath}, size = {insize}")

# custom resolution set
if argc > 3:
    res_re = re.compile('([0-9]+)?x([0-9]+)?')
    res_str = sys.argv[3]
    width = -1
    height = -1
    if (match := res_re.match(res_str)) is not None:
        width_str = match.group(1)
        height_str = match.group(2)
        if width_str:
            width = int(width_str)
        if height_str:
            height = int(height_str)
    if width < 0 and height < 0:
        print(f"Invalid resolution: {res_str}")
        sys.exit(1)
    if width < 0:
        width = -(insize // -height) # ceil division
    elif height < 0:
        height = -(insize // -width)
    elif width * height < insize:
        print(f"Resolution {width}x{height} too small to draw {insize} bytes")
        sys.exit(1)
    IMSIZE = (width, height)
    IMLEN = IMSIZE[0] * IMSIZE[1]
    info(f"Using custom resolution: {width}x{height}, size = {IMLEN}")

if insize < IMLEN:
    padsize = IMLEN - insize
    print(f"Padding up data by {padsize} bytes")
    indata = indata + bytes([0] * padsize)
print(f"Creating Image from raw data into buffer of size {IMSIZE} -> {IMLEN}")
outdata = Image.frombuffer('P', IMSIZE, indata)

print("Loading standard VGA palette")
palfile = open('images/vga.pal')
palette = []
while (line := palfile.readline()) is not None:
    if not line:
        break
    for v in line.strip().split(','):
        if not v:
            continue
        palette.append(int(v))

# these were extracted from the mgraphic.exe overlay, used by function 44 to set the dac registers
pal0 = [ 0, 0, 0, 0, 0, 0x2a, 0, 0x2a, 0, 0, 0x2a, 0x2a, 0x2a, 0,
         0, 0x2a, 0, 0x2a, 0x2a, 0x15, 0, 0x2a, 0x2a, 0x2a, 0x15, 0x15,
         0x15, 0x15, 0x15, 0x3f, 0x15, 0x3f, 0x15, 0x15, 0x3f, 0x3f, 0x3f,
         0x15, 0x15, 0x3f, 0x15, 0x3f, 0x3f, 0x3f, 0x15, 0x3f, 0x3f, 0x3f ]
pal1 = [ 0, 0, 0, 0, 0, 0x2a, 0, 0x2a, 0, 0, 0x2a, 0x2a, 0x2a, 0,
         0, 0, 0, 0, 0x2a, 0x15, 0, 0x2a, 0x2a, 0x2a, 0x15, 0x15, 0x15,
         0x15, 0x15, 0x3f, 0x15, 0x3f, 0x15, 0x15, 0x3f, 0x3f, 0x3f, 0x15,
         0x15, 0x3f, 0x15, 0x3f, 0x3f, 0x3f, 0x15, 0x3f, 0x3f, 0x3f ]
pal2 = [ 0, 0, 0, 0, 0, 0x2a, 0, 0x2a, 0, 0, 0x2a, 0x2a, 0x2a, 0,
         0, 0x2a, 0, 0x2a, 0x2a, 0x15, 0, 0x2a, 0x2a, 0x2a, 0x15, 0x15,
         0x15, 0x15, 0x15, 0x3f, 0x15, 0x3f, 0x15, 0x15, 0x3f, 0x3f, 0x3f,
         0x15, 0x15, 0, 0, 0, 0x3f, 0x3f, 0x15, 0x3f, 0x3f, 0x3f ]
pal3 = [ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ]
pal4 = [ 0, 0, 0, 0, 0, 0x2a, 0, 0, 0, 0, 0x2a, 0x2a, 0x2a, 0, 0,
         0x2a, 0, 0x2a, 0x2a, 0x15, 0, 0x2a, 0x2a, 0x2a, 0x15, 0x15, 0x15,
         0x15, 0x15, 0x3f, 0, 0, 0, 0x15, 0x3f, 0x3f, 0x3f, 0x15, 0x15,
         0x3f, 0x15, 0x3f, 0x3f, 0x3f, 0x15, 0x3f, 0x3f, 0x3f ]

paldict = { '0': pal0, '1': pal1, '2': pal2, '3': pal3, '4': pal4 }

if argc >= 3 :
    palstr = sys.argv[2]
    if palstr != 'x':
        print(f"Overriding default vga palette with custom palette {palstr}")
        palovr = paldict[palstr]
        if len(palovr) != 3*16:
            print("Invalid length of palette overlay!")
            sys.exit(1)
        for i in range(len(palovr)):
            palette[i] = palovr[i] * 4

pallen = len(palette)
if pallen != 3*256:
    print(f"Unexpected palette length: {pallen}")
    sys.exit(1)

print("Attaching palette to image")
outdata.putpalette(palette)
print(f"Saving to {outpath}")
outdata.save(outpath)