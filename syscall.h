#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

class Memory;
class Cpu;

class Dos {
private:
    Cpu* cpu_;
    Memory* memory_;
    Offset freeMem_;

private:
    static constexpr Size PSP_SIZE = 0x100;

#pragma pack(push, 1)
    struct ProgramSegmentPrefix {
        Word exit_instr;        // 00h-01h: 2 bytes (code), CP/M-80-like exit (always contains INT 20h)[1]
        Word byte_past_segment; // 02h-03h: word (2 bytes), Segment of the first byte beyond the memory allocated to the program
        Byte reserved_1;        // 04h    : byte          , Reserved
        Byte dos_far_call1[5];  // 05h-09h: 5 bytes (code), CP/M-80-like far call entry into DOS, and program segment size[1][2]
        Dword prev_term_addr;   // 0Ah-0Dh: dword         , Terminate address of previous program (old INT 22h)
        Dword prev_break_addr;  // 0Eh-11h: dword         , Break address of previous program (old INT 23h)
        Dword prev_err_addr;    // 12h-15h: dword         , Critical error address of previous program (old INT 24h)
        Word parent_psp_seg;    // 16h-17h: word          , Parent's PSP segment (usually COMMAND.COM - internal)
        Byte jft[20];           // 18h-2Bh: 20 bytes      , Job File Table (JFT) (internal)
        Word env_seg;           // 2Ch-2Dh: word          , Environment segment
        Dword last_stack_addr;  // 2Eh-31h: dword         , SS:SP on entry to last INT 21h call (internal)
        Word jft_size;          // 32h-33h: word          , JFT size (internal)
        Dword jft_ptr;          // 34h-37h: dword         , Pointer to JFT (internal)
        Dword prev_psp;         // 38h-3Bh: dword         , Pointer to previous PSP (only used by SHARE in DOS 3.3 and later)
        Dword reserved_2;       // 3Ch-3Fh: 4 bytes       , Reserved
        Word dos_ver;           // 40h-41h: word          , DOS version to return (DOS 4 and later, alterable via SETVER in DOS 5 and later)
        Byte reserved_3[14];    // 42h-4Fh: 14 bytes      , Reserved
        Byte dos_far_call2[3];  // 50h-52h: 3 bytes (code), Unix-like far call entry into DOS (always contains INT 21h + RETF)
        Word reserved_4;        // 53h-54h: 2 bytes       , Reserved
        Byte reserved_5[7];     // 55h-5Bh: 7 bytes       , Reserved (can be used to make first FCB into an extended FCB)
        Byte fcb_1[16];         // 5Ch-6Bh: 16 bytes      , Unopened Standard FCB 1
        Byte fcb_2[20];         // 6Ch-7Fh: 20 bytes      , Unopened Standard FCB 2 (overwritten if FCB 1 is opened)
        Byte cmdline_size;      // 80h    : 1 byte        , Number of bytes on command-line
        Byte cmdline[127];      // 81h-FFh: 127 bytes     , Command-line tail (terminated by a 0Dh)[3][4]
    };
    static_assert(sizeof(ProgramSegmentPrefix) == PSP_SIZE, "Invalid PSP structure size!");
#pragma pack(pop)

public:
    Dos(Cpu* cpu, Memory* memory);
};

#endif // SYSCALL_H
