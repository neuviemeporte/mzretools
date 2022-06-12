
#include <iostream>
#include <algorithm>
#include <fstream>
#include <cassert>

#include "dos/dos.h"
#include "dos/memory.h"
#include "dos/psp.h"
#include "dos/mz.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

static bool print_message = true;
void dosMessage(const string &msg) {
    if (print_message) cout << msg << endl;
}

Dos::Dos(Memory *memory) : memory_(memory) {
}

LoadAddresses Dos::loadExe(MzImage &mz) {
    const Address freeStart{memory_->freeStart()};
    const Size memSize = memory_->availableBytes(),
               memBlock = memory_->availableBlock();
    Address pspAddr = freeStart;
    // align PSP to paragraph boundary
    if (pspAddr.offset != 0) {
        pspAddr.segment += 1;
        pspAddr.offset = 0;
    }
    dosMessage("Free DOS memory: "s + to_string(memSize) + ", starts at address "s + freeStart.toString());
    Address loadAddr(pspAddr.toLinear() + PSP_SIZE);
    assert(loadAddr.offset == 0);
    dosMessage("Determined address of PSP: "s + pspAddr.toString() + ", load module: " + loadAddr.toString());
    const Size 
        loadModuleSize = mz.loadModuleSize(),
        minSize = PSP_SIZE + loadModuleSize + mz.minAlloc(),
        maxSize = PSP_SIZE + loadModuleSize + mz.maxAlloc(),
        minBlock = BYTES_TO_PARA(minSize),
        maxBlock = BYTES_TO_PARA(maxSize);
    dosMessage("Load module is "s + to_string(loadModuleSize) + " / " + hexVal(loadModuleSize) + " bytes, min alloc = " 
                + to_string(minSize) + ", max = " + to_string(maxSize));
    if (minBlock > memBlock) throw DosError("Minimum alloc size of " + to_string(minSize) + " bytes exceeds available memory: " + to_string(memSize));
    const Size allocBlock = min(memBlock, maxBlock);
    assert(allocBlock % PARAGRAPH_SIZE == 0);
    // allocate memory
    memory_->allocBlock(allocBlock);
    dosMessage("Allocated memory: "s + to_string(allocBlock) + " paragraphs, free mem at " + Address{memory_->freeStart()}.toString());
    // blit psp data over to memory
    // TODO: implement cmdline
    ProgramSegmentPrefix psp;
    const Byte *pspData = reinterpret_cast<const Byte*>(&psp);
    memory_->writeBuf(pspAddr.toLinear(), pspData, PSP_SIZE);
    // read load module data from exe file into memory
    const Offset loadModuleOffset = mz.loadModuleOffset();
    dosMessage("Loading from offset "s + hexVal(loadModuleOffset) + ", size = " + hexVal(mz.loadModuleSize()));
    mz.load(loadAddr.segment);
    const Byte *exeData = mz.loadModuleData();
    memory_->writeBuf(loadAddr.toLinear(), exeData, mz.loadModuleSize());
    // calculate relocated addresses for code and stack
    LoadAddresses ret;
    ret.code = mz.codeAddress();
    ret.stack = mz.stackAddress();
    ret.code.segment += loadAddr.segment;
    ret.stack.segment += loadAddr.segment;
    return ret;
}

