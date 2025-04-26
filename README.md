# mzretools

This is a collection of utilities to help with reverse engineering MS-DOS games. These can analyze MZ executables containing 8086 opcodes and provide information that is useful in reverse engineering. For now, .com files and newer Intel CPUs are not supported.

In general, these tools were written to be used in tandem with IDA, but could also potentially be adopted into alternate workflows. The idea is to support an iterative approach to creating an instruction-level-identical reconstruction, while allowing for internal executable layout differences (the routine/data offsets don't need to match). Here is an example of how all the tools might be used together:

![process diagram](diagrams/process-stable.svg)

A detailed write-up in the form of a blog post of how I use these in my own project can be found [here](https://neuviemeporte.github.io/f15-se2/2024/05/25/sausage.html).

The project contains a unit test suite written with GTest that runs by default on every build. The build system is CMake.

Patches and improvement suggestions are welcome. I hope this can be of use to somebody.

# building

Clone repository, then do:

```
# download the google test framework for unit tests
$ git submodule init
$ git submodule update
$ mkdir build && cd build
$ cmake ..
# this will build the tool executables and run the test suite
$ make
```

The test suite can also be run independently after building, by executing the `runtest` binary. It also supports some undocumented flags for debug output, see `test_main.cpp`.

# tools

This is still work in progress, and I am adding features as needed in my own reversing efforts. Currently, the array of useful tools consists of the following:

## mzhdr

Shows information from the .exe file MZ header:

```
ninja@dell:debug$ ./mzhdr bin/hello.exe
--- bin/hello.exe MZ header (28 bytes)
        [0x0] signature = 0x5a4d ('MZ')
        [0x2] last_page_size = 0x43 (67 bytes)
        [0x4] pages_in_file = 15 (7680 bytes)
        [0x6] num_relocs = 4
        [0x8] header_paragraphs = 32 (512 bytes)
        [0xa] min_extra_paragraphs = 227 (3632 bytes)
        [0xc] max_extra_paragraphs = 65535
        [0xe] ss:sp = 207:800
        [0x12] checksum = 0x156e
        [0x16] cs:ip = 0:20
        [0x18] reloc_table_offset = 0x1e
        [0x1a] overlay_number = 0
--- extra data (overlay info?)
        0x01, 0x00
--- relocations:
        [0]: 173:1a, linear: 0x174a, file offset: 0x194a, file value = 0x16f
        [1]: 0:2b, linear: 0x2b, file offset: 0x22b, file value = 0x16f
        [2]: 0:b5, linear: 0xb5, file offset: 0x2b5, file value = 0x16f
        [3]: 173:ae, linear: 0x17de, file offset: 0x19de, file value = 0x16f
--- load module @ 0x200, size = 0x1a43 / 6723 bytes
```

Also has some additional switches that make it useful in scripts, like print just the load module offset/size (`-l`, `-s`). With `-p`, it can patch an executable's relocations as if loaded at a provided segment and write the result to a file. This is useful for comparing with a memory dump obtained at runtime to detect corruption and/or other problems, see also `exeimgdump.sh`.

## mzmap

Scans and interprets instructions in the executable, traces jump/call destinations and return instructions in order to try and determine the boundaries of subroutines and offsets of potential variables. It can do limited register/stack value tracing to figure out register-dependent calls and jumps. Reachable blocks are either attributed to a subroutine's main body, or it can be marked as a disconnected chunk. The map is saved to a file in a text format.

```
mzmap v1.0.0
usage: mzmap [options] [file.exe[:entrypoint]] file.map
Scans a DOS MZ executable trying to find routines and variables, saves output into an editable map file
Without an exe file, prints a summary of an existing map file
There is limited support for using an IDA .lst file as a map file,
and printing a .lst file will also convert and save it as a .map file
Options:
--verbose:      show more detailed information, including compared instructions
--debug:        show additional debug information
--overwrite:    overwrite output file.map if already exists
--brief:        only show uncompleted and unclaimed areas in map summary
--format:       format printed routines in a way that's directly writable back to the map file
--nocpu:        omit CPU-related information like instruction decoding from debug output
--noanal:       omit analysis-related information from debug output
--linkmap file  use a linker map from Microsoft C to seed initial location of routines
--load segment: override default load segment (0x0)
ninja@dell:debug$ ./mzmap bin/hello.exe hello.map --verbose
Loading executable bin/hello.exe at segment 0x1000
Analyzing code within extents: 1000:0000-11a4:0003/001a44
Done analyzing code
Dumping visited map of size 0x1a43 starting at 0x10000 to routines.visited
Dumping visited map of size 0x1a43 starting at 0x10000 to search_queue.dump
Building routine map from search queue contents (39 routines)
--- Routine map containing 39 routines
STACK/0x1207
CODE/0x1000
DATA/0x116f
1000:0000-1000:000f/000010: unclaimed_1
1000:0010-1000:001f/000010: routine_7
1000:0020-1000:00d1/0000b2: start
1000:00d2-1000:0195/0000c4: routine_4
        1000:00d2-1000:0195/0000c4: main
        1000:0262-1000:0267/000006: chunk
1000:0196-1000:01ac/000017: routine_8
1000:01ad-1000:01f1/000045: routine_9
1000:01f2-1000:021e/00002d: routine_13
1000:021f-1000:022d/00000f: routine_10
[...]
1000:1574-1000:15e1/00006e: routine_34
1000:15e2-1000:1637/000056: routine_35
1000:1638-1000:165d/000026: unclaimed_13
1000:165e-1000:1680/000023: routine_19
1000:1681-1000:1a42/0003c2: unclaimed_14
Saving routine map (size = 39) to hello.map
```

## mzdiff

Takes two executable files as input and compares their instructions one by one to verify if they match, which is useful when trying to recreate the source code of a game in a high level programming language. After compiling the recreation, this tool can instantly check to see if the generated code matches the original. It accounts for data layout differences, so if one executable accesses a value at one memory offset, and the other has it at a different offset, the mapping between the two is saved, and not counted as a mismatch as long as its use is consistent. It can optionally take the map generated by mzmap as an input, which enables assigning meaningful names to the compared subroutines, as well as to exclude some subroutines from the comparison, like standard library functions, assembly subroutines or others that are not eligible for comparison for some other reason.

The tool also has the ability to compare data segment contents with `--data segname`. You will need map files for both executables, although the target one may be rudimentary; it only needs the address of the data segment with the specified name (a `.map.tgt` file obtained from a mzdiff code comparison run should work fine for this purpose). The tool will scan and compare the contents of both files at the location of the specified data segment byte by byte, and halt with an error if a mismatch is found, in which case it will also try to determine which variable the mismatch ocurred in (if variable information is present in the reference map), and also a hex diff will be shown between the two executables around the mismatch location. This is not rocket science, but it saves the hassle of extracting the data segments to binary files, obtaining hexdumps with `xxd`, loading them up in WinMerge for visual inspection, and then figuring out which location in the executable the identified hexdump offset corresponds to.

```
mzdiff v1.0.8
usage: mzdiff [options] reference.exe[:entrypoint] target.exe[:entrypoint]
Compares two DOS MZ executables instruction by instruction, accounting for differences in code layout
Options:
--map ref.map    map file of reference executable (recommended, otherwise functionality limited)
--tmap tgt.map   map file of target executable (only used for output and in data comparison)
--verbose        show more detailed information, including compared instructions
--debug          show additional debug information
--dbgcpu         include CPU-related debug information like instruction decoding
--idiff          ignore differences completely
--nocall         do not follow calls, useful for comparing single functions
--asm            descend into routines marked as assembly in the map, normally skipped
--nostat         do not display comparison statistics at the end
--rskip count    ignore up to 'count' consecutive mismatching instructions in the reference executable
--tskip count    ignore up to 'count' consecutive mismatching instructions in the target executable
--ctx count      display up to 'count' context instructions after a mismatch (default 10)
--loose          non-strict matching, allows e.g for literal argument differences
--variant        treat instruction variants that do the same thing as matching
--data segname   compare data segment contents instead of code
The optional entrypoint spec tells the tool at which offset to start comparing, and can be different
for both executables if their layout does not match. It can be any of the following:
  ':0x123' for a hex offset
  ':0x123-0x567' for a hex range
  ':[ab12??ea]' for a hexa string to search for and use its offset. The string must consist of an even amount
   of hexa characters, and '??' will match any byte value.
In case of a range being specified as the spec, the search will stop with a success on reaching the latter offset.
If the spec is not present, the tool will use the CS:IP address from the MZ header as the entrypoint.
ninja@dell:debug$ ./mzdiff original.exe:10 my.exe:10 --verbose --loose --sdiff 1 --map original.map
Comparing code between reference (entrypoint 1000:0010/010010) and other (entrypoint 1000:0010/010010) executables
Reference location @1000:0010/010010, routine 1000:0010-1000:0482/000473: main, block 010010-010482/000473, other @1000:0010/010010
1000:0010/010010: push bp                          == 1000:0010/010010: push bp
1000:0011/010011: mov bp, sp                       == 1000:0011/010011: mov bp, sp
1000:0013/010013: sub sp, 0x1c                     =~ 1000:0013/010013: sub sp, 0xe 🟡 The 'loose' option ignores the stack layout difference
1000:0016/010016: push si                          == 1000:0016/010016: push si
1000:0017/010017: mov word [0x628c], 0x0           ~= 1000:0017/010017: mov word [0x418], 0x0 🟡 same value placed at different offsets in the two .exe-s
1000:001d/01001d: mov word [0x628a], 0x4f2         ~= 1000:001d/01001d: mov word [0x416], 0x4f2
1000:0023/010023: mov word [0x45fc], 0x0           ~= 1000:0023/010023: mov word [0x410], 0x0
[...]
1000:0111/010111: or ax, ax                        == 1000:010e/01010e: or ax, ax
1000:0113/010113: jnz 0x11c (down)                 ~= 1000:0110/010110: jnz 0x102 (up)
1000:0115/010115: call far 0x16b50c7f              ~= 1000:0112/010112: call far 0x10650213
1000:011a/01011a: jmp 0x11e (down)                 != 1000:0117/010117: cmp byte [0x40c], 0x78 🔴 the 'skip difference = 1' option allows one mismatching instruction
1000:011c/01011c: jmp 0x105 (up)                   != 1000:0117/010117: cmp byte [0x40c], 0x78 🔴 but the next one still doesn't match
ERROR: Instruction mismatch in routine main at 1000:011c/01011c: jmp 0x105 != 1000:0117/010117: cmp byte [0x40c], 0x78
```

The output shows `==` for an exact match, `~=` and `=~` for a "soft" difference in either the first or second operand, and `!=` for a mismatch. The idea is to iterate on the reconstruction process as long as the tool finds discrepancies, until the reconstructed code perfectly matches the original, with a margin for the different layout resulting in offset value differences.

## mzsig

Extracts routine signatures from an executable and saves them to a file. The signatures are strings of hex values representing the routines' instructions, but with addresses and immediate values stripped away - `mov [bx+0x1234], 0xabcd` becomes roughly `mov|mem|immediate`. The idea is to get rid of any information that's specific to a particular executable, but retain enough of the "flavour" of the routine so that it can hopefully be identified in a different executable.

```
mzsig v1.0.0
Usage:
mzsig [options] exe_file map_file output_file
    Extracts routines from exe_file at locations specified by map_file and saves their signatures to output_file
    This is useful for finding routine duplicates from exe_file in other executables using mzdup
mzsig [options] signature_file
    Displays the contents of the signature file
Options:
--verbose       show information about extracted routines
--debug         show additional debug information
--overwrite     overwrite output file if exists
--min count     ignore routines smaller than 'count' instructions (default: 10)
--max count     ignore routines larger than 'count' instructions (0: no limit, default: 0)
You can prevent specific routines from having their signatures extracted by annotating them with 'ignore' in the map file,
see the map file format documentation for more information.
ninja@RYZEN:f15se2-re$ mzsig ../ida/egame.exe map/egame.map egame.sig
Loaded map file map/egame.map: 8 segments, 400 routines, 1023 variables
Extracted signatures from 294 routines, saving to egame.sig
```

When passed a single argument, the tool can be used to print the abstract instructions contained within the signature strings of a signature file that has already been created:

```
ninja@RYZEN:f15se2-re$ mzsig egame.sig
Loaded signatures from egame.sig, 294 routines
routine_7: 100 instructions
        push bp
        mov bp, sp
        sub sp, i16
        mov [bp+off16], i16
        mov [bp+off16], i16
        les bx, [bp+off16]
        mov ax, es:[bx]
        mov [off16], ax
        [...]
```

## mzdup

This utility can be used to find potential duplicates of known routines from one executable in another. It uses edit distance on strings of ambiguated instructions obtained from `mzsig` with a threshold to locate the potential duplicates and save time on reversing already known routines. The minimum number of instructions that a routine must have to be considered for duplicate search, as well as the maximum distance at which the routines are still considered duplicates, are both configurable through commandline options.

```
mzdup v1.0.0
Usage: mzdup [options] signature_file target_exe target_map
Attempts to identify duplicate routines matching the signatures from the input file in the target executable
Updated target_map with duplicates marked will be saved to target_map.dup
Options:
--verbose:       show more detailed information about processed routines
--debug:         show additional debug information
--minsize count: don't search for duplicates of routines smaller than 'count' instructions (default: 15)
--maxdist ratio: how many instructions relative to its size can a routine differ by to still be reported as a duplicate (default: 10%)
ninja@RYZEN:f15se2-re$ mzdup egame.sig ../ida/start.exe map/start.map
Searching for duplicates of 294 signatures among 255 candidates, minimum instructions: 15, maximum distance ratio: 10%
Processed 294 signatures, ignored 51 as too short
Tried to find matches for 255 target exe routines (7500 instructions, 100%)
Found 37 (unique: 37) matching routines (1415 instructions, 18%)
Unable to find 218 matching routines (82%)
Saving code map (routines = 255) to map/start.map.dup, reversing relocation by 0x1000
```

In the usage case above, the tool determined (using an edit distance tolerance threshold of 10% of the routine length) that about 18% of the code in the known routines of `start.exe` matches up with some of the routines from `egame.exe`. The updated map file saved with a `.dup` suffix can be examined to see which exact routines were classified as potential duplicates, and running the tool with `--verbose` will show the calculated edit distances as well. The minimum considered routine size can be lowered (--minsize), or the tolerance increased (--maxdist) to make the search more aggressive, at the risk of getting more false positives.

## mzptr

This tool accepts an executable binary and its map, then it tries to find potential pointers to known variables within the executable's binary image. These are important, because often a numeric value can be overlooked as just that instead of a pointer to an another place. This saves some effort by brute forcing the data references, but still some manual effort is required to look over the obtained results and determine if the found references are genuine pointers.

```
ninja@RYZEN:f15se2-re$ mzptr
mzptr v0.9.6
Usage: mzptr [options] exe_file exe_map
Searches the executable for locations where offsets to known data objects could potentially be stored.
Results will be written to standard output; these should be reviewed and changed to references to variables during executable reconstruction.
Options:
--verbose:       show more detailed information about processed routines
--debug:         show additional debug information
ninja@RYZEN:f15se2-re$ mzptr ../ida/start.exe map/start.map
Search complete, found 528 potential references, unique: 132
Printing reference counts per variable, counts higher than 1 or 2 are probably false positives due to a low/non-characteristic offset
word_16BE2/16b5:0092/016be2: 1 reference @ Data1:0xa8
page1Num/16b5:0530/017080: 1 reference @ Data1:0x546
page2Num/16b5:0548/017098: 1 reference @ Data1:0x55e
unk_170B0/16b5:0560/0170b0: 1 reference @ Data1:0x576
aLibya/16b5:00c2/016c12: 1 reference @ Data1:0x578
aVietnam/16b5:00d5/016c25: 1 reference @ Data1:0x57c
[...]
aRookie/16b5:016e/016cbe: 1 reference @ Data1:0x58c
aPilot/16b5:0175/016cc5: 1 reference @ Data1:0x58e
aVeteran/16b5:017b/016ccb: 1 reference @ Data1:0x590
aAce/16b5:0183/016cd3: 1 reference @ Data1:0x592
aDemo/16b5:0187/016cd7: 1 reference @ Data1:0x594
[...]
aMsRunTimeLibra/16b5:0008/016b58: 20 references
unk_16B56/16b5:0006/016b56: 34 references
aOnc_2/16b5:0700/017250: 39 references
unk_16B57/16b5:0007/016b57: 45 references
byte_16B54/16b5:0004/016b54: 87 references
crt0_16B52/16b5:0002/016b52: 118 references
```

The results are sorted by the found reference count; items with low reference counts are more plausible than items with a high count, like the `aMsRunTimeLibra` string, whose offset was "found" in 20 places only by virtue of being a small, common value of `0x8`.

Items with the same reference count are further sorted by the offset where the match ocurred, which helps to see adjacent locations forming arrays of pointers, like the array of the difficulty level strings at `0x58c`.

## lst2ch.py

This Python script will parse an IDA-generated listing `.LST` file and generate a C header file with routine and data declarations, so they can be plugged into a C source code reconstrucion. It saves manual effort in updating the headers when routine names or routine arguments change in IDA. It can also output a C source file with data definitions, but this is more of a prototype for now. It will verify the running size of the data segment as it's iterating over the listing using two independent methods. It shares a JSON config file with the subsequent tool, `lst2asm.py` to specify the layout of the listing and the transformations needed to be performed on it. Below is a sample config file used in my reconstruction effort:

```json
{
    // stuff to put at the beginning of the assembly file generated by lst2asm
    "preamble": [
        ".8086",
        ".MODEL SMALL",
        "DGROUP GROUP startData,startBss",
        "ASSUME DS:DGROUP",
        "__acrtused = 9876h",
        "PUBLIC __acrtused"
    ],
    "include": "lst/start.inc",
    "in_segments": [ "startCode1", "startCode2", "startData", "startStack" ],
    "out_segments": [
        { "seg": "startCode1", "class": "CODE" },
        { "seg": "startCode2", "class": "CODE" },
        { "seg": "startData",  "class": "DATA" },
        { "seg": "startBss", "class": "BSS" },
        { "seg": "startStack",  "class": "STACK" }
    ],
    "code_segments": [ "startCode1", "startCode2" ],
    "data_segments": [ "startData", "startBss" ],
    // the sum of the size of all the items in the data segment, used to verify 
    // that the assembly file contains all the data
    "data_size": "0x7b10",
    // locations and contents of input lines that should be replaced in the assembly output
    "replace": [ 
        // can modify segment definitions on the fly
        { "seg": "startCode1", "off": "0x0", "from": "startCode1 segment", 
            "to": "startCode1 segment word public 'CODE'" },
        { "seg": "startData", "off": "0x0", "from": "startData segment", 
            "to": "startData segment word public 'DATA'" },
        // modify instructions into literal bytes to enforce specific encoding
        { "seg": "startCode1", "off": "0x2ff", "to": "db 05h, 48h, 0" },
        { "seg": "startCode1", "off": "0x376a", "to": "db 3Dh, 10h, 0" }, 
        // modify segment termination to force closing of a different segment
        { "seg": "startData",  "off": "0x7afc", "from": "ends", "to": "startBss ends" }
    ],
    // locations and contents of lines that should be inserted into the assembly output
    "insert": [
        // modify location in the data segment to create a BSS segment there
        { "seg": "startData", "off": "0x4585", "from": "db", "to": [ 
                "startData ends", 
                "startBss segment byte public \"BSS\""
            ]
        }
    ],
    // blocks that should be eliminated from the input, usually because they have been 
    // already ported to C code
    "extract": [ 
        // a bunch of initial zeros at the beginning of the code segment caused by .DOSSEG
        { "seg": "startCode1", "begin": "0x10",   "end": "0x482",  "from": "main",
            "to": "endp" },
        { "seg": "startCode1", "begin": "0x4a0",  "end": "0x510",  "from": "initGraphics",
            "to": "endp" },
        { "seg": "startCode1", "begin": "0x511",  "end": "0x543",  "from": "cleanup",
            "to": "endp" },
        { "seg": "startCode1", "begin": "0x54a",  "end": "0x560",  "from": "clearKeybuf",
            "to": "endp" },
    ],
    // list of procs that should be copied into the output assembly from the input, if this list is empty, 
    // then all procs will be preserved (copied).
    // Otherwise (list not empty), procs not in this list will just have a single "retn" instruction inside, 
    // so that they are marked as needing porting because the comparison tool will trip on them. 
    // Use this to mark routines which cannot or are not expected to be ported for some reason.
    "preserves" : [
        "installCBreakHandler", "setupOverlaySlots", "setTimerIrqHandler", "timerIrqHandler", "loadOverlay", 
        "restoreTimerIrqHandler", "copyJoystickData", "restoreCbreakHandler"
    ],
    // list of symbols to declare as external in the output assembly (because they were already ported to C code)
    "externs": [ "main", "waitMdaCgaStatus", "openFileWrapper", "closeFileWrapper", "cleanup", 
        "choosePilotPrompt", "pilotToGameData", "clearBriefing", "seedRandom", "gameDataToPilot", "saveHallfame"
    ],
    // list of symbols to declare as public in the output assembly, so that they are visible from C code
    "publics": [ 
       "byte_17412", "byte_17422", "word_173DE", "asc_174AC", "byte_1741A", "asc_174AF", "ranks", "pilotNameInputColors",
        "aMenterYourName", "copyJoystickData", "installCBreakHandler", "loadOverlay", "restoreCbreakHandler", 
        "setTimerIrqHandler"
    ],
    // stuff to write at the beginning of the C header file generated by lst2ch
    "header_preamble": [
        "#ifndef F15_SE2_START",
        "#define F15_SE2_START",
        "#include \"inttype.h\"",
        "#include \"struct.h\"",
        "#include \"comm.h\"",
        "",
        "#include <stdio.h>",
        "",
        "#if !defined(MSDOS) && !defined(__MSDOS__)",
        "#define far",
        "#endif",
        "",
        "#define __int32 long",
        "#define __int8 char",
        "#define __cdecl",
        "#define __far far"
    ],
    // likewise, stuff to write at the end
    "header_coda": "#endif // F15_SE2_START"
}
```

This is pretty obscure at first glance, but not all features need to be utilized for it to be useful.

## lst2asm.py

Similarly to the previous script, this parses the IDA listing but the output is assembly code that has been transformed according to the instructions in the config file, and whose purpose is to be assembled with a MASM-compatible assembler, and linked with the C source code being reconstructed to form a full game executable, with parts being rewritten into C, and parts still being left in assembly while the reconstruction is being worked on.

## dosbuild.sh

This is a bash script which wraps running the DOS build toolchain (compiler, assembler, linker) inside headless DosBox, to be used inside Makefiles so it's possible to build code for DOS from the comfort of the Linux command line without needing to go into actual DOS.

## other

There's a bunch of bash scripts for doing odd jobs as well.
