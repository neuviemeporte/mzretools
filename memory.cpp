#include <iostream>
#include <fstream>
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