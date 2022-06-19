#include <iostream>
#include <iomanip>
#include <sstream>
#include "dos/address.h"
#include "dos/error.h"
#include "dos/util.h"

using namespace std;

Address::Address(const Offset linear) {
    if (linear >= MEM_TOTAL) throw MemoryError("Linear address too big while converting to segmented representation: "s + hexVal(linear));
    set(linear);
}

void Address::set(const Offset linear) {
    segment = static_cast<Word>(linear >> SEGMENT_SHIFT);
    offset = static_cast<Word>(linear & OFFSET_MASK);
}

std::string Address::toString(const bool brief) const {
    ostringstream str;
    str << std::hex << std::setw(WORD_STRLEN) << std::setfill('0') << segment << ":" << std::setw(WORD_STRLEN) << std::setfill('0') << offset;
    if (!brief) str << "/" << setw(OFFSET_STRLEN) << toLinear();
    return str.str();
}

// move bulk of the offset to the segment part, limit offset to the modulus of a paragraph
void Address::normalize() {
    segment += offset >> SEGMENT_SHIFT;
    offset &= OFFSET_MASK;
}

void Address::rebase(const Word base) {
    auto linear = toLinear(), baseLinear = SEG_OFFSET(base);
    if (linear < baseLinear) throw MemoryError("Unable to rebase address " + toString() + " to " + hexVal(base));
    linear -= baseLinear;
    set(linear);
}

std::ostream& operator<<(std::ostream &os, const Address &arg) {
    return os << arg.toString();
}

std::string Block::toString(const bool linear) const {
    if (!isValid()) return "[invalid]";
    ostringstream str;
    if (!linear) str << begin.toString(true) << "-" << end.toString(true);
    else str << hexVal(begin.toLinear(), false, OFFSET_STRLEN) << "-" << hexVal(end.toLinear(), false, OFFSET_STRLEN);
    str << "/" << hex << setw(OFFSET_STRLEN) << setfill('0') << size();
    return str.str();
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