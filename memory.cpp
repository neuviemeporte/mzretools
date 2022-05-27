#include <iostream>
#include <fstream>
#include <sstream>

#include "memory.h"
#include "error.h"
#include "mz.h"
#include "util.h"

using namespace std;

MemoryRange::MemoryRange(Offset begin, Offset end) : begin(begin), end(end) {
    if (end < begin) throw MemoryError("Invalid range extents");
}

Address::Address(const Offset linear) {
    if (linear >= MEM_TOTAL) throw MemoryError("Linear address too big while converting to segmented representation");
    segment = static_cast<Word>(linear >> 4);
    offset = static_cast<Word>(linear & 0xf);
}

Address::operator std::string() const {
    ostringstream str;
    str << *this;
    return str.str();
}

std::string Address::toString() const {
    return static_cast<string>(*this);
}

void Address::normalize() {
    segment += offset >> 4;
    offset &= 0xf;
}

std::ostream& operator<<(std::ostream &os, const Address &arg) {
    return os << std::hex << std::setw(4) << std::setfill('0') << arg.segment 
        << ":" << std::setw(4) << std::setfill('0') << arg.offset << " / 0x" << arg.toLinear();
}

Arena::Arena() : break_(INIT_BREAK) {
    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
    
    for (Offset i = 0, j = 0; i < MEM_TOTAL; ++i) {
        // initialize memory data with initial pattern
        data_[i] = pattern[j];
        if (++j >= sizeof pattern) j = 0;
    }
}

void Arena::allocBlock(const Size para) {
    const Size size = para * PARAGRAPH_SIZE;
    if (break_ + size <= MEM_END)
        break_ += size;
    else
        throw MemoryError("No room to allocate "s + to_string(size) + ", avail = "s + to_string(availableBytes()));
}

void Arena::freeBlock(const Size para) {
    const Size size = para * PARAGRAPH_SIZE;
    if (break_ - size >= INIT_BREAK)
        break_ -= size;
    else
        throw MemoryError("No room to free "s + to_string(size) + ", avail = "s + to_string(availableBytes()));
}

Byte Arena::readByte(const Offset addr) const {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Read byte outside memory bounds"));
    return data_[addr];
}

Word Arena::readWord(const Offset addr) const {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Read word outside memory bounds"));
    return *reinterpret_cast<const Word*>(&data_[addr]);
}

void Arena::writeByte(const Offset addr, const Byte value) {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Byte write outside memory bounds"));
    data_[addr] = value;
}

void Arena::writeWord(const Offset addr, const Word value) {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Word write outside memory bounds"));
    Word *dest = reinterpret_cast<Word*>(&data_[addr]);
    *dest = value;
}

string Arena::info() const {
    ostringstream infoStr;
    infoStr << "total size = " << MEM_TOTAL << " / " << MEM_TOTAL / KB << " kB @" << hexVal(reinterpret_cast<Offset>(base())) << ", "
            << "available = " << availableBytes() << " / " << availableBytes() / KB << " kB";
    return infoStr.str();
}

void Arena::dump(const std::string &path) const {
    FILE *dumpFile = fopen(path.c_str(), "w");
    if (!dumpFile) throw IoError("Unable to open file for arena dump: " + path);
    cout << "Dumping arena contents to " << path << endl;
    auto arenaPtr = pointer(0);
    Size totalSize = 0;
    while (totalSize < MEM_TOTAL) {
        auto amount = fwrite(arenaPtr, 1, PAGE_SIZE, dumpFile);
        arenaPtr += amount;
        totalSize += amount;
    }
    fclose(dumpFile);
}