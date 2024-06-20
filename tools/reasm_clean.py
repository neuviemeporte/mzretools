#!/usr/bin/env python3
import sys
import re

in_asmfile = 'src/f15-se2/start_raw.asm'
out_asmfile = 'src/f15-se2/start_re.asm'
codestart_re = re.compile('; File Name.*start.exe')
in_asm = open(in_asmfile, 'r', encoding='cp437')
out_asm = open(out_asmfile, 'w', encoding='cp437')

publics = [ 'loc_12A83', 'errorDescAndExit', 'errorAndExit', 'loc_156D2', 'loc_15C14', 'loc_155E5', 'loc_16500', 'loc_1650B', 'loc_16907' ]
substitutions = [ 
    (0, 'add ax, COMM_JOYDATA_OFF', 'db 5,48h,0'),
    (0, 'cmp ax, DOS_ERROR_RMDIR', 'db 3Dh,10h,0'),
    (0, 'db 29h dup(?)', 'startData ends\nstartBss segment byte public \'BSS\'\ndb 29h dup(?)'),
    (0, 'startData ends', 'startBss ends')
]

instr_alu_ax_re = re.compile('(cmp|add|and|sub|sbb) ax, ([0-9a-fA-F]{1,5}h?)')

curline = 0
written = 0
skip = False

out_asm.write('DGROUP GROUP startData,startBss\nASSUME DS:DGROUP')
for line in in_asm:
    curline += 1

    # deactivate skip
    if skip:
        if codestart_re.match(line):
            skip = False

    # append extra colon to public labels
    for p in publics:
        if line.startswith(f'{p}:'):
            line = f'{p}::\n'

    # substitutions
    instruction = " ".join(line.split())
    comment_idx = instruction.find(';')
    if comment_idx >= 0:
        instruction = instruction[0:comment_idx]
    instruction = instruction.strip()
    if instruction == 'align 2':
        line = '\t\tnop\n'
    elif instruction == 'align 4':
        line = '\t\tnop\n\t\tnop\n'
        written += 1
    elif instruction == 'jmp far ptr 0:0' or instruction.startswith('jmp far ptr gfx') or instruction.startswith('jmp far ptr byte_') or instruction.startswith('jmp far ptr misc_') or instruction.startswith('jmp gfx_'):
        line = '\t\tdb 0EAh,0,0,0,0 ; jmp far ptr 0:0\n'
    elif instruction == 'repne movsw':
        line = '\t\tdb 0F2h,0A5h ; repne movsw\n'
    elif instruction == 'or di, 2':
        line = '\t\tdb 81h,0CFh,2,0 ; or di, 2\n'
    elif (match := instr_alu_ax_re.fullmatch(instruction)) is not None:
        line = '\t\tdb '
        if match.group(1) == 'cmp':
            line+='3Dh\n'
        elif match.group(1) == 'add':
            line+='05h\n'
        elif match.group(1) == 'and':
            line+='25h\n'
        elif match.group(1) == 'sub':
            line+='2Dh\n'
        elif match.group(1) == 'sbb':
            line+='1Dh\n'
        else:
            print(f"line {curline}: unexpected {match.group(1)}")
            sys.exit(1)
        line += f'\t\tdw {match.group(2)} ; {instruction}\n'
    elif instruction.startswith('assume'):
        line = f'; {instruction}\n'
    else:
        for s in substitutions:
            # substitution for a specific line, skip unless line reached
            if s[0] != 0 and curline != s[0]:
                continue
            # substitution for a specific line, make sure substituted instr is the expected one
            if instruction != s[1]:
                if s[0] != 0:
                    print(f"unexpected instruction on line {curline}; expected {s[1]}, found {instruction}")
                    sys.exit(1)
                else:
                    continue
            line = f'\t\t{s[2]} ; {instruction}\n'

    # write out line
    if not skip:
        out_asm.write(line)
        written += 1

    # activate skip
    if line.strip().startswith('.model'):
        skip = True

print(f"Written out {written} lines out of {curline} from {in_asmfile} to {out_asmfile}")

