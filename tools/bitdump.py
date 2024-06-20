#!/usr/bin/env python3
# 
# A utility for dumping file contents using a custom number of bits per byte,
# which is useful for examining LZW-encoded files
#
import sys

def error(msg):
    print(msg)
    sys.exit(1)

def info(msg):
    print(msg)


# TODO: endianness?
def process_file(file, bytebits, binary):
    bytes_per_line = 16
    # create bytemask
    bytemask = 1
    for i in range(bytebits - 1):
        bytemask <<= 1
        bytemask |= 1
    hexdigits = len(f"{bytemask:x}")
    info(f"Using bytemask {bytemask:0b}")
    acc = 0;
    acc_bits = 0
    bytecount = 0
    fileoffset = 0
    # accumulate and display custom bitlength 'bytes'
    while True:
        if (bytecount == 0): # new display line, print offset header
            print(f'[{fileoffset:#x}.{acc_bits}]: ', end='')
        # read in 8 bits at a time
        x = file.read(1)
        if not x:
            break
        fileoffset += 1
        # put the new bits into the accumulator
        acc <<= 8
        acc |= x[0]
        acc_bits += 8
        while acc_bits >= bytebits:
            value = (acc >> acc_bits - bytebits) & bytemask
            acc_bits -= bytebits
            if not binary:
                print(f"{value:0{hexdigits}x} ", end='')
            else:
                print(f"{value:0{bytebits}b} ", end='')
            bytecount += 1
            if (bytecount == bytes_per_line):
                bytecount = 0
                print('')
    # TODO: output any leftover bits
    print('')


def main(filename, bits, binary):
    info(f"Dumping contents of {filename} with {bits} bits per byte")
    with open(filename, 'rb') as f:
        process_file(f, bits, binary)

if __name__ == '__main__':
    argc = len(sys.argv)
    if argc < 3 or argc > 4:
        error(f'Syntax: bitdump.py filename bits-per-byte [-b]')

    binary = False # default is hex output
    filename = sys.argv[1]
    bits = int(sys.argv[2])
    if argc == 4:
        arg = sys.argv[3]
        print("arg: " + arg)
        if arg == '-b':
            binary = True
        else:
            error("Unrecognized argument: " + arg)
    main(filename, bits, binary)