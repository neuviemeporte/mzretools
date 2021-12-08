#include "dos.h"
#include "memory.h"
#include "cpu.h"
#include "mz.h"
#include "util.h"

#include <iostream>

using namespace std;

Dos::Dos(Cpu *cpu, Arena *memory) : _cpu(cpu), _memory(memory) {
}

void Dos::loadExe(const MzImage &mz) {
    // TODO: put PSP in first
    const Offset 
        arenaLoadOffset = _memory->freeStart(),
        moduleLoadOffset = mz.loadModuleOffset();
    const Size 
        loadModuleSize = mz.loadModuleSize(),
        allocSize = PSP_SIZE + loadModuleSize;
    _memory->alloc(allocSize);
    ProgramSegmentPrefix psp;

    // cout << "Loading to offset 0x" << hex << arenaLoadOffset << ", size = " << dec << loadModuleSize << endl;
    // auto ptr = pointer(arenaLoadOffset);
    // const auto path = mz.path();
    // FILE *mzFile = fopen(path.c_str(), "r");
    // if (fseek(mzFile, moduleLoadOffset, SEEK_SET) != 0) 
    //     throw IoError("Unable to seek to load module!");
    // Size totalSize = 0;
    // while (totalSize < loadModuleSize) {
    //     // TODO: read into side buffer of appropriate size, memcpy to arena, otherwise unable to ensure no overflow. Rethink arena api for mz loading.
    //     auto size = fread(ptr, 1, PAGE, mzFile);
    //     ptr += size;
    //     totalSize += size;
    //     cout << "Read " << size << ", read size = " << totalSize << endl;
    // }
    // if (totalSize != loadModuleSize) {
    //     ostringstream msg("Unexpected load module size read (", ios_base::ate);
    //     msg << hex << totalSize << " vs expected " << loadModuleSize << ")";
    //     throw IoError(msg.str());
    // }
    // fclose(mzFile);
    // // adjust break position after loading to memory
    // break_ += loadModuleSize;

    // TODO: patch relocations
    // for (const auto &rel : relocs_) {

    // }
}

std::ostream& operator<<(std::ostream &os, const ProgramSegmentPrefix &arg) {
    return os << std::hex
        << "exit_instr = 0x" << hexString(arg.exit_instr) << endl
        << "byte_past_segment = 0x" << arg.byte_past_segment << endl
        << "reserved_1 = 0x" << static_cast<int>(arg.reserved_1) << endl
        << "dos_far_call1 = 0x" << hexString(arg.dos_far_call1) << endl
        << "prev_term_addr = 0x" << arg.prev_term_addr << endl
        << "prev_break_addr = 0x" << arg.prev_break_addr << endl
        << "prev_err_addr = 0x" << arg.prev_err_addr << endl
        << "parent_psp_seg = 0x" << arg.parent_psp_seg << endl
        << "jft = 0x" << hexString(arg.jft) << endl
        << "env_seg = 0x" << arg.env_seg << endl
        << "last_stack_addr = 0x" << arg.last_stack_addr << endl
        << "jft_size = 0x" << arg.jft_size << endl
        << "jft_ptr = 0x" << arg.jft_ptr << endl
        << "prev_psp = 0x" << arg.prev_psp << endl
        << "reserved_2 = 0x" << arg.reserved_2 << endl
        << "dos_ver = 0x" << arg.dos_ver << endl
        << "reserved_3 = 0x" << hexString(arg.reserved_3) << endl
        << "dos_far_call2 = 0x" << hexString(arg.dos_far_call2) << endl
        << "reserved_4 = 0x" << arg.reserved_4 << endl
        << "reserved_5 = 0x" << hexString(arg.reserved_5) << endl
        << "fcb_1 = 0x" << hexString(arg.fcb_1) << endl
        << "fcb_2 = 0x" << hexString(arg.fcb_2) << endl
        << "cmdline_size = 0x" << static_cast<int>(arg.cmdline_size) << endl
        << "cmdline = 0x" << hexString(arg.cmdline) << endl;
}