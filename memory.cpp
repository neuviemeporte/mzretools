#include <iostream>
#include <fstream>
#include <sstream>

#include "memory.h"
#include "error.h"
#include "mz.h"

using namespace std;

MemoryRange::MemoryRange(Offset begin, Offset end) : begin(begin), end(end) {
    if (end < begin) throw MemoryError("Invalid range extents");
}

SegmentedAddress::SegmentedAddress(const Offset linear) {
    if (linear >= MEM_TOTAL) throw MemoryError("Linear address too big while converting to segmented representation");
    segment = static_cast<Reg16>(linear >> 4);
    offset = static_cast<Reg16>(linear & 0xf);
}

void SegmentedAddress::normalize() {
    segment += offset >> 4;
    offset &= 0xf;
}

std::ostream& operator<<(std::ostream &os, const SegmentedAddress &arg) {
    return os << std::hex << arg.segment << ":" << arg.offset;
}

Arena::Arena() :
    break_(INIT_BREAK) {
    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
    
    for (Offset i = 0, j = 0; i < MEM_TOTAL; ++i) {
        // initialize memory data with initial pattern
        data_[i] = pattern[j];
        if (++j >= sizeof pattern) j = 0;
    }
}

Byte Arena::read(const Offset addr) const {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Read outside memory bounds"));
    return data_[addr];
}

void Arena::write(const Offset addr, const Byte value) {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Byte write outside memory bounds"));
    data_[addr] = value;
}

void Arena::write(const Offset addr, const Word value) {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Word write outside memory bounds"));
    Word *dest = reinterpret_cast<Word*>(&data_[addr]);
    *dest = value;
}

void Arena::loadMz(const MzImage &mz) {
    // TODO: put PSP in first
    const Offset 
        arenaLoadOffset = freeStart(),
        moduleLoadOffset = mz.loadModuleOffset();
    const Size loadModuleSize = mz.loadModuleSize();
    if (arenaLoadOffset + loadModuleSize >= freeEnd())
        throw ArgError("Loading image of size " + to_string(loadModuleSize) + " into arena would overflow free memory!");
    cout << "Loading to offset 0x" << hex << arenaLoadOffset << ", size = " << dec << loadModuleSize << endl;
    auto ptr = pointer(arenaLoadOffset);
    const auto path = mz.path();
    FILE *mzFile = fopen(path.c_str(), "r");
    if (fseek(mzFile, moduleLoadOffset, SEEK_SET) != 0) 
        throw IoError("Unable to seek to load module!");
    Size totalSize = 0;
    while (totalSize < loadModuleSize) {
        // TODO: read into side buffer of appropriate size, memcpy to arena, otherwise unable to ensure no overflow. Rethink arena api for mz loading.
        auto size = fread(ptr, 1, PAGE, mzFile);
        ptr += size;
        totalSize += size;
        cout << "Read " << size << ", read size = " << totalSize << endl;
    }
    if (totalSize != loadModuleSize) {
        ostringstream msg("Unexpected load module size read (", ios_base::ate);
        msg << hex << totalSize << " vs expected " << loadModuleSize << ")";
        throw IoError(msg.str());
    }
    fclose(mzFile);
    // adjust break position after loading to memory
    break_ += loadModuleSize;

    // TODO: patch relocations
    // for (const auto &rel : relocs_) {

    // }
}

void Arena::dump(const std::string &path) const {
    FILE *dumpFile = fopen(path.c_str(), "w");
    if (!dumpFile) throw IoError("Unable to open file for arena dump: " + path);
    cout << "Dumping arena contents to " << path << endl;
    auto arenaPtr = pointer(0);
    Size totalSize = 0;
    while (totalSize < MEM_TOTAL) {
        auto amount = fwrite(arenaPtr, 1, PAGE, dumpFile);
        arenaPtr += amount;
        totalSize += amount;
    }
    fclose(dumpFile);
}