#include "dos.h"
#include "memory.h"
#include "cpu.h"
#include "mz.h"
#include "util.h"

#include <iostream>

using namespace std;

static bool print_message = true;

void dosMessage(const string &msg) {
    if (print_message) cout << msg << endl;
}

ProgramSegmentPrefix::ProgramSegmentPrefix() :
    exit_instr(0x20cd),
    byte_past_segment(0),
    reserved_1(0),
    prev_term_addr(0),
    prev_break_addr(0),
    prev_err_addr(0),
    parent_psp_seg(0),
    env_seg(0),
    last_stack_addr(0),
    jft_size(0),
    jft_ptr(0),
    prev_psp(0),
    reserved_2(0),
    dos_ver(3),
    reserved_4(0),
    cmdline_size(0)
{
    memset(dos_far_call1, 0, 5]);
    memset(jft, 0, 20);
    memset(reserved_3, 0, 14);
    memset(dos_far_call2, 0, 3);
    memset(reserved_5, 0, 7);
    memset(fcb_1, 0, 16);
    memset(fcb_2, 0, 20);
    memset(cmdline, 0, 127);
}

Dos::Dos(Cpu *cpu, Arena *memory) : _cpu(cpu), _memory(memory) {
}

void Dos::loadExe(const MzImage &mz) {
    const Offset 
        freeMemStart = _memory->freeStart(),
        moduleLoadOffset = mz.loadModuleOffset();
    SegmentedAddress freeMemAddr(freeMemStart);
    dosMessage("Free DOS memory starts at offset "s + static_cast<std::string>(freeMemAddr));
    // align PSP to paragraph boundary
    Word pspSegment = freeMemAddr.segment;
    if (freeMemAddr.offset != 0) {
        pspSegment += 1;
    }
    Word loadSegment = pspSegment + 1;
    dosMessage("Determined PSP segment: "s + hexVal(pspSegment) + ", load segment: " + hexVal(loadSegment));
    // allocate minimum memory
    const Size 
        loadModuleSize = mz.loadModuleSize(),
        allocSize = PSP_SIZE + loadModuleSize + mz.minAlloc();
    _memory->alloc(allocSize);
    ProgramSegmentPrefix psp;
    // TODO: implement cmdline


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