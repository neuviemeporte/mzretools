#include <iostream>
#include <iomanip>
#include <sstream>
#include "dos/address.h"
#include "dos/error.h"
#include "dos/util.h"

using namespace std;

Address::Address(const Offset linear) {
    if (linear >= MEM_TOTAL) throw MemoryError("Linear address too big while converting to segmented representation: "s + hexVal(linear));
    segment = static_cast<Word>(linear >> 4);
    offset = static_cast<Word>(linear & 0xf);
}

std::string Address::toString(const bool brief) const {
    ostringstream str;
    str << std::hex << std::setw(4) << std::setfill('0') << segment << ":" << std::setw(4) << std::setfill('0') << offset;
    if (!brief) str << " / 0x" << toLinear();
    return str.str();
}

void Address::normalize() {
    segment += offset >> 4;
    offset &= 0xf;
}

std::ostream& operator<<(std::ostream &os, const Address &arg) {
    return os << arg.toString();
}

std::string Block::toString() const {
    if (isValid()) return "["s + begin + ", " + end + "]";
    else return "[invalid]";
}

bool Block::intersects(const Block &other) const {
    const Offset 
        maxBegin = std::max(begin.toLinear(), other.begin.toLinear()),
        minEnd   = std::min(end.toLinear(), other.end.toLinear());
    return (maxBegin <= minEnd);
}

Block Block::coalesce(const Block &other) const {
    if (!intersects(other)) return {};
    return { 
        std::min(begin.toLinear(), other.begin.toLinear()),
        std::max(end.toLinear(), other.end.toLinear())
     };
}

std::ostream& operator<<(std::ostream &os, const Block &arg) {
    return os << arg.toString();
}