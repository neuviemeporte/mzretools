# mzretools

This is a collection of utilities to help with reverse engineering MS-DOS games. These can analyze MZ executables containing 8086 opcodes and provide information that is useful in reverse engineering. For now, .com files and newer Intel CPUs are not supported.

In general, these tools were written to be used in tandem with IDA, but could also potentially be adopted into alternate workflows. The idea is to support an iterative approach to creating an instruction-level-identical reconstruction, while allowing for internal executable layout differences (the routine/data offsets don't need to match). Here is an example of how all the tools might be used together:

![process diagram](diagrams/process-stable.svg)

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

## mzmap

Scans and interprets instructions in the executable, traces jump/call destinations and return instructions in order to try and determine the boundaries of subroutines. It can do limited register value tracing to figure out register-dependent calls and jumps. Reachable blocks are either attributed to a subroutine's main body, or it can be marked as a disconnected chunk. The map is saved to a file in a text format.

```
usage: mzmap [options] [<file.exe[:entrypoint]>] <output.map>
Scans a DOS MZ executable and tries to find routine boundaries, saves output into an editable map file
Without an exe file, prints a summary of an existing map file
Options:
--verbose:      show more detailed information, including compared instructions
--debug:        show additional debug information
--hide:         only show uncompleted and unclaimed areas in map summary
--format:       format printed routines in a way that's directly writable back to the map file
--nocpu:        omit CPU-related information like instruction decoding
--noanal:       omit analysis-related information
--load segment: overrride default load segment (0x1000)
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

```
usage: mzdiff [options] base.exe[:entrypoint] compare.exe[:entrypoint]
Compares two DOS MZ executables instruction by instruction, accounting for differences in code layout
Options:
--map basemap  map file of base executable to use, if present
--verbose      show more detailed information, including compared instructions
--debug        show additional debug information
--dbgcpu       include CPU-related debug information like instruction decoding
--idiff        ignore differences completely
--nocall       do not follow calls, useful for comparing single functions
--rskip count  skip differences, ignore up to 'count' consecutive mismatching instructions in the reference executable
--tskip count  skip differences, ignore up to 'count' consecutive mismatching instructions in the target executable
--ctx count    display up to 'count' context instructions after a mismatch (default 10)
--loose        non-strict matching, allows e.g for literal argument differences
--variant      treat instruction variants that do the same thing as matching
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
    // the sum of the size of all the items in the data segment, used to verify that the assembly file contains all the data
    "data_size": "0x7b10",
    // locations and contents of input lines that should be replaced in the assembly output
    "replace": [ 
        // can modify segment definitions on the fly
        { "seg": "startCode1", "off": "0x0", "from": "startCode1 segment", "to": "startCode1 segment word public 'CODE'" },
        { "seg": "startData", "off": "0x0", "from": "startData segment", "to": "startData segment word public 'DATA'" },
        // modify instructions into literal bytes to enforce specific encoding
        { "seg": "startCode1", "off": "0x2ff", "to": "db 05h, 48h, 0" },
        { "seg": "startCode1", "off": "0x376a", "to": "db 3Dh, 10h, 0" }, 
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
    // blocks that should be eliminated from the input, usually because they have been already ported to C code
    "extract": [ 
        // a bunch of initial zeros at the beginning of the code segment caused by .DOSSEG
        { "seg": "startCode1", "begin": "0x10",   "end": "0x482",  "from": "main",              "to": "endp" },
        { "seg": "startCode1", "begin": "0x4a0",  "end": "0x510",  "from": "initGraphics",      "to": "endp" },
        { "seg": "startCode1", "begin": "0x511",  "end": "0x543",  "from": "cleanup",           "to": "endp" },
        { "seg": "startCode1", "begin": "0x54a",  "end": "0x560",  "from": "clearKeybuf",       "to": "endp" },
    ],
    // list of procs that should be copied into the output assembly from the input, if this list is empty, then all procs will be preserved (copied).
    // Otherwise (list not empty), procs not in this list will just have a single "retn" instruction inside, so that they are marked as needing porting because the comparison tool will trip on them. Use this to mark routines which cannot or are not expected to be ported for some reason
    "preserves" : [
        "installCBreakHandler", "setupOverlaySlots", "setTimerIrqHandler", "timerIrqHandler", "loadOverlay", "restoreTimerIrqHandler", "copyJoystickData", "restoreCbreakHandler"
    ],
    // list of symbols to declare as external in the output assembly (because they were already ported to C code)
    "externs": [ "main", "waitMdaCgaStatus", "openFileWrapper", "closeFileWrapper", "cleanup", "choosePilotPrompt", "pilotToGameData", "clearBriefing", "seedRandom", "gameDataToPilot", "saveHallfame", "processStoreInput", 
    "pilotNameInput" ],
    // list of symbols to declare as public in the output assembly, so that they are visible from C code
    "publics": [ 
       "byte_17412", "byte_17422", "word_173DE", "asc_174AC", "byte_1741A", "asc_174AF", "ranks", "pilotNameInputColors", "aMenterYourName", 
        "copyJoystickData", "installCBreakHandler", "loadOverlay", "restoreCbreakHandler", "setTimerIrqHandler", "setupOverlaySlots", "getTimeOfDay", "cbreakHit", "movedata", "word_17282", "sub_16A7F", "a_3dg", "aOpenErrorOn__0", "aRb_0", "word_18188", "gridBuf1", 
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

This is pretty obscure at first glance, but not all features need to be used for it to be useful.

## lst2asm.py

Similarly to the previous script, this parses the IDA listing but the output is assembly code that has been transformed according to the instructions in the config file, and whose purpose is to be assembled with a MASM-compatible assembler, and linked with the C source code being reconstructed to form a full game executable, with parts being rewritten into C, and parts still being left in assembly while the reconstruction is being worked on.

## dosbuild.sh

This is a bash script which wraps running the DOS build toolchain (compiler, assembler, linker) inside headless DosBox, to be used inside Makefiles so it's possible to build code for DOS from the comfort of the Linux command line without needing to go into actual DOS.

## other

There's a bunch of bash scripts for doing odd jobs as well.
