#include <iostream>
#include <iomanip>
#include <sstream>
#include <regex>
#include "dos/address.h"
#include "dos/error.h"
#include "dos/util.h"

using namespace std;

static const regex 
    FARADDR_RE{"([0-9a-fA-F]{1,4}):([0-9a-fA-F]{1,4})"},
    HEXOFFSET_RE{"0x([0-9a-fA-F]{1,5})"},
    DECOFFSET_RE{"([0-9]{1,7})"},
    HEXSIZE_RE{"\\+0x([0-9a-fA-F]{1,5})"},
    DECSIZE_RE{"\\+([0-9]{1,7})"};

Address::Address(const Offset linear) {
    set(linear);
}

Address::Address(const std::string &str) {
    smatch match;
    if (regex_match(str, match, FARADDR_RE)) {
        segment = stoi(match.str(1), nullptr, 16),
        offset  = stoi(match.str(2), nullptr, 16);
    }
    else if (regex_match(str, match, HEXOFFSET_RE)) {
        set(stoi(match.str(1), nullptr, 16));
    }
    else if (regex_match(str, match, DECOFFSET_RE)) {
        set(stoi(match.str(1), nullptr, 10));
    }
    else
        throw ArgError("Invalid address string: "s + str);
}

void Address::set(const Offset linear) {
    if (linear >= MEM_TOTAL) throw MemoryError("Linear address too big while converting to segmented representation: "s + hexVal(linear));
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

Block::Block(const std::string &from, const std::string &to) {
    begin = Address{from};
    smatch match;
    if (regex_match(to, match, HEXSIZE_RE)) {
        size_t size = stoi(match.str(1), nullptr, 16);
        end = begin + size;
    }
    else if (regex_match(to, match, DECSIZE_RE)) {
        size_t size = stoi(match.str(1), nullptr, 10);
        end = begin + size;
    }
    else {
        end = Address(to);
    }
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