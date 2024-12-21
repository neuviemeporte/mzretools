#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

#include "dos/memory.h"
#include "dos/error.h"
#include "dos/mz.h"
#include "dos/util.h"

using namespace std;

Memory::Memory() : break_(INIT_BREAK) {
    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
    
    for (Offset i = 0, j = 0; i < MEM_TOTAL; ++i) {
        // initialize memory data with initial pattern
        data_[i] = pattern[j];
        if (++j >= sizeof pattern) j = 0;
    }
}

Memory::Memory(const Word segment, const Byte *data, const Size size) : Memory() {
    writeBuf(SEG_TO_OFFSET(segment), data, size);
}

void Memory::allocBlock(const Size para) {
    const Size size = para * PARAGRAPH_SIZE;
    if (break_ + size <= MEM_END)
        break_ += size;
    else
        throw MemoryError("No room to allocate "s + to_string(size) + ", avail = "s + to_string(availableBytes()));
}

void Memory::freeBlock(const Size para) {
    const Size size = para * PARAGRAPH_SIZE;
    if (break_ - size >= INIT_BREAK)
        break_ -= size;
    else
        throw MemoryError("No room to free "s + to_string(size) + ", avail = "s + to_string(availableBytes()));
}

Byte Memory::readByte(const Offset addr) const {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Read byte outside memory bounds"));
    return data_[addr];
}

Word Memory::readWord(const Offset addr) const {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Read word outside memory bounds"));
    Word ret;
    memcpy(&ret, &data_[addr], sizeof(ret));
    return ret;
}

void Memory::writeByte(const Offset addr, const Byte value) {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Byte write outside memory bounds"));
    data_[addr] = value;
}

void Memory::writeWord(const Offset addr, const Word value) {
    if (addr >= MEM_TOTAL) throw MemoryError(std::string("Word write outside memory bounds"));
    memcpy(&data_[addr], &value, sizeof(value));
}

void Memory::writeBuf(const Offset addr, const Byte *data, const Size size) {
    if (addr + size > MEM_TOTAL) throw MemoryError(std::string("Buffer write outside memory bounds"));
    copy(data, data + size, begin(data_) + addr);
}

// TODO: use std::search with custom predicate to cover -1 case
Address Memory::find(const ByteString &pattern, Block where) const {
    if (!where.isValid()) where = { {0}, {MEM_TOTAL - 1} };
    const Offset start = where.begin.toLinear();
    const Offset end = std::min(where.end.toLinear(), MEM_TOTAL);
    if (start >= end) throw AddressError("Invalid search range: " + where.toString());
    const Size patSize = pattern.size();
    Address found;
    vector<Byte> buffer(patSize);
    for (Offset dataIdx = start; dataIdx + patSize < end; ++dataIdx) {
        std::copy(data_.begin() + dataIdx, data_.begin() + dataIdx + patSize, buffer.begin());
        //debug("dataIdx = " + hexVal(dataIdx) + ", buffer: " + byteBufString(buffer));
        bool match = true;
        // ouch, O(n^2)
        for (Offset patIdx = 0; patIdx < pattern.size(); ++patIdx) {
            const SWord pat = pattern[patIdx];
            //debug(hexVal(pat, false) + " ");
            if (pat == -1) continue;
            if (pat != buffer[patIdx]) { match = false; break; }
        }
        if (match) { found = Address{dataIdx}; break; }
    }
    return found;
}

string Memory::info() const {
    ostringstream infoStr;
    infoStr << "total size = " << MEM_TOTAL << " / " << MEM_TOTAL / KB << " kB @" << hexVal(reinterpret_cast<Offset>(base())) << ", "
            << "available = " << availableBytes() << " / " << availableBytes() / KB << " kB";
    return infoStr.str();
}

void Memory::dump(const Block &range, const std::string &path) const {
    auto memoryPtr = pointer(range.begin.toLinear());
    if (path.empty()) { // dump to screen
        hexDump(memoryPtr, range.size(), 0, false);
    }
    else { // dump to file
        ofstream dumpFile{path, ios::binary};
        if (!dumpFile) 
            throw IoError("Unable to open file for Memory dump: " + path);
        dumpFile.write(reinterpret_cast<const char*>(memoryPtr), range.size());
    }
}