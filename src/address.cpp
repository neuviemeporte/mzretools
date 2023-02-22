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
    set(linear, true);
}

Address::Address(const std::string &str) {
    smatch match;
    if (regex_match(str, match, FARADDR_RE)) {
        segment = stoi(match.str(1), nullptr, 16),
        offset  = stoi(match.str(2), nullptr, 16);
    }
    else if (regex_match(str, match, HEXOFFSET_RE)) {
        set(stoi(match.str(1), nullptr, 16), true);
    }
    else if (regex_match(str, match, DECOFFSET_RE)) {
        set(stoi(match.str(1), nullptr, 10), true);
    }
    else
        throw ArgError("Invalid address string: "s + str);
}

Address::Address(const Address &other, const SWord displacement) : segment(other.segment), offset(other.offset) {
    offset += displacement;
}

void Address::set(const Offset linear, const bool normal) {
    if (linear >= MEM_TOTAL) throw MemoryError("Linear address too big while converting to segmented representation: "s + hexVal(linear));
    segment = static_cast<Word>(linear >> SEGMENT_SHIFT);
    offset = static_cast<Word>(linear & OFFSET_NORMAL_MASK);
    if (normal) normalize();
}

Address Address::operator+(const DWord arg) const {
    Offset linear = toLinear();
    return Address{ linear + arg };
}

std::string Address::toString(const bool brief) const {
    ostringstream str;
    if (isValid()) {
        str << std::hex << std::setw(WORD_STRLEN) << std::setfill('0') << segment << ":" << std::setw(WORD_STRLEN) << std::setfill('0') << offset;
        if (!brief) str << "/" << setw(OFFSET_STRLEN) << toLinear();
    }
    else str << "(invalid)";
    return str.str();
}

// move bulk of the offset to the segment part, limit offset to the modulus of a paragraph
void Address::normalize() {
    segment += offset >> SEGMENT_SHIFT;
    offset &= OFFSET_NORMAL_MASK;
}

void Address::rebase(const Word base) {
    auto linear = toLinear(), baseLinear = SEG_TO_OFFSET(base);
    if (linear < baseLinear) throw MemoryError("Unable to rebase address " + toString() + " to " + hexVal(base));
    linear -= baseLinear;
    set(linear, true);
}

std::ostream& operator<<(std::ostream &os, const Address &arg) {
    return os << arg.toString();
}

Block::Block(const std::string &blockStr) {
    static const regex BLOCK_RE{"([0-9a-fA-F]{1,6})-([0-9a-fA-F]{1,6})"};
    smatch match;
    if (!regex_match(blockStr, match, BLOCK_RE))
        throw ArgError("Invalid block string: "s + blockStr);
    const Offset 
        beginVal = stoi(match.str(1), nullptr, 16),
        endVal   = stoi(match.str(2), nullptr, 16);
    begin = Address{beginVal};
    end = Address{endVal};
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
    if (!isValid())
        throw ArgError("Invalid memory block range");
}

std::string Block::toString(const bool linear, const bool showSize) const {
    if (!isValid()) return "[invalid]";
    ostringstream str;
    if (!linear) str << begin.toString(true) << "-" << end.toString(true);
    else str << hexVal(begin.toLinear(), false, OFFSET_STRLEN) << "-" << hexVal(end.toLinear(), false, OFFSET_STRLEN);
    if (showSize) str << "/" << hex << setw(OFFSET_STRLEN) << setfill('0') << size();
    return str.str();
}

// check if blocks overlap each other (by at least one byte)
bool Block::intersects(const Block &other) const {
    if (!isValid()) return false;
    const Address
        maxBegin = std::max(begin, other.begin),
        minEnd   = std::min(end, other.end);
    return maxBegin <= minEnd;
}

// check if blocks are right next to each other (but disjoint)
bool Block::adjacent(const Block &other) const {
    if (!isValid()) return false;
    const Address
        maxBegin = std::max(begin, other.begin),
        minEnd   = std::min(end, other.end);
    return (maxBegin > minEnd) && (maxBegin - minEnd == 1);
}

void Block::coalesce(const Block &other) {
    if (!intersects(other) && !adjacent(other)) return;
    begin = std::min(begin, other.begin);
    end = std::max(end, other.end);
}

Block Block::coalesce(const Block &other) const {
    Block ret = *this;
    ret.coalesce(other);
    return ret;
}

std::ostream& operator<<(std::ostream &os, const Block &arg) {
    return os << arg.toString();
}