#include "dos.h"

Dos::Dos(Cpu *cpu, Memory *memory) : cpu_(cpu), memory_(memory), freeMem_(0) {
    static_assert(sizeof(ExeHeader) == EXE_HEADER_SIZE, "Invalid EXE header size");
    static_assert(sizeof(ExeRelocation) == EXE_RELOC_SIZE, "Invalid EXE relocation entry size!");
}
