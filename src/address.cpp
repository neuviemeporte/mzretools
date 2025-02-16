#include <iostream>
#include <iomanip>
#include <sstream>
#include <regex>
#include <algorithm>

#include "dos/address.h"
#include "dos/error.h"
#include "dos/util.h"
#include "dos/output.h"

using namespace std;

OUTPUT_CONF(LOG_MEMORY)

static const regex 
    FARADDR_RE{"([0-9a-fA-F]{1,4}):([0-9a-fA-F]{1,4})"},
    HEXOFFSET_RE{"0x([0-9a-fA-F]{1,5})"},
    DECOFFSET_RE{"([0-9]{1,7})"},
    HEXSIZE_RE{"\\+0x([0-9a-fA-F]{1,5})"},
    DECSIZE_RE{"\\+([0-9]{1,7})"};

Address::Address(const Offset linear) {
    set(linear);
}

Address::Address(const std::string &str, const bool fixNormal) {
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
    else throw ArgError("Invalid address string: "s + str);
    if (fixNormal) normalize();
}

Address::Address(const Address &other, const SWord displacement) : segment(other.segment), offset(other.offset) {
    offset += displacement;
}

void Address::set(const Offset linear) {
    if (linear >= MEM_TOTAL) throw MemoryError("Linear address too big while converting to segmented representation: "s + hexVal(linear));
    segment = static_cast<Word>((linear & SEGMENT_MASK) >> SEGMENT_SHIFT);
    offset = static_cast<Word>(linear & OFFSET_MASK);
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

// advance segment by specified amount, relocate(234:a, 0x1000) -> 1234:a
void Address::relocate(const Word reloc) { 
    if (segment > OFFSET_MAX - reloc) throw MemoryError("Unable to relocate address " + toString() + " by " + hexVal(reloc));
    segment += reloc; 
}

// inverse of relocate, e.g. rebase(1234:a, 0x1000) -> 234:a
void Address::rebase(const Word base) {
    if (base > segment) throw MemoryError("Unable to rebase address " + toString() + " to " + hexVal(base));
    segment -= base;
}

// move(1234:a, 1000) -> 1000:234a, effective address remains the same
void Address::move(const Word arg) {
    if (!inSegment(arg)) throw MemoryError("Unable to move address "s + toString() + " to segment " + hexVal(arg));
    offset = toLinear() - SEG_TO_OFFSET(arg);
    segment = arg;
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
    ostringstream str;
    if (!isValid()) str << "[invalid] ";
    if (!linear) str << begin.toString(true) << "-" << end.toString(true);
    else str << hexVal(begin.toLinear(), false, OFFSET_STRLEN) << "-" << hexVal(end.toLinear(), false, OFFSET_STRLEN);
    if (showSize) str << "/" << hex << setw(OFFSET_STRLEN) << setfill('0') << size();
    return str.str();
}

std::string Block::toHex() const {
    ostringstream str;
    str << hexVal(begin.offset, false) << "-" << hexVal(end.offset, false);
    return str.str();
}

// check if blocks overlap each other (by at least one byte)
bool Block::intersects(const Block &other) const {
    if (!isValid() || !other.isValid()) return false;
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

bool Block::singleSegment() const { 
    if (!begin.inSegment(end.segment)) throw AddressError("Unable to flatten block to single segment: " + toString());
    return begin.segment == end.segment; 
}

// split this block into multiple blocks so that no block is larger than a segment
std::vector<Block> Block::splitSegments() const {
    if (!isValid()) throw AddressError("Unable to split block into segments: " + toString());
    vector<Block> ret;
    Size span = size();
    debug("Splitting block " + toString());
    Address blockStart = begin;
    while (span != 0) {
        const Word maxSpan = OFFSET_MAX - blockStart.offset;
        debug("Next block starting at " + blockStart.toString() + ", remaining span: " + sizeStr(span) + ", max: " + sizeStr(maxSpan));
        Block b{blockStart};
        // remaining span will not fit in current segment, pad up to OFFSET_MAX and advance to the next one
        if (span > maxSpan) { 
            b.end = Address{b.begin.segment, OFFSET_MAX};
            debug("Closed non-final block up to segment boundary: " + b.toString());
            blockStart.segment += 0x1000;
            blockStart.offset = 0;
        }
        // remaining span fits in current segment, make the last block to consume it whole
        else {
            b.end = Address{b.begin.segment, static_cast<Word>(b.begin.offset + span - 1)};
            debug("Closed final block: " + b.toString());
            assert(b.size() == span);
        }
        ret.push_back(b);
        assert(b.size() <= span);
        span -= b.size();
    }
    return ret;
}

// given an another, potentially intersecting block, cut this one into 0/1/2 parts, so that the blocks don't intersect anymore (essentially an exclusive-or)
std::vector<Block> Block::cut(const Block &other) const {
    if (!isValid() || !other.isValid()) return {};    
    vector<Block> ret;
    // other starts before begin of this
    if (other.begin < begin) {
        // disjoint before
        if (other.end < begin) ret.push_back(*this);
        // other intersects without enclosing
        else if (other.end < end) ret.push_back(Block{other.end + Offset(1), end});
        // other encloses, empty set as output
    }
    // other starts inside of this
    else if (other.begin < end) {
        if (other.begin > begin) ret.push_back(Block{begin, other.begin - 1});
        if (other.end < end) ret.push_back(Block{other.end + Offset(1), end});
    }
    // other stats after end of this, disjoint after
    else ret.push_back(*this);

    return ret;
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

Segment::Segment(const std::smatch &match) {
    if (match.empty()) throw ArgError("Segment string mismatch");
    name = match.str(1);
    address = stoi(match.str(3), nullptr, 16);
    const string segtype = match.str(2);
    if (segtype == "CODE") type = SEG_CODE;
    else if (segtype == "DATA") type = SEG_DATA;
    else if (segtype == "STACK") type = SEG_STACK;
    else throw ArgError("Invalid segment type string: " + segtype);
}

std::smatch Segment::stringMatch(const std::string &str) {
    static const regex SEG_RE{"^([$_a-zA-Z0-9]+) (CODE|DATA|STACK) ([0-9a-fA-F]{1,4})"};
    smatch match;
    regex_match(str, match, SEG_RE);
    return match;
}

std::string Segment::typeString(const Type t) {
    switch (t) {
    case SEG_NONE: return "NONE";
    case SEG_CODE: return "CODE";
    case SEG_DATA: return "DATA";
    case SEG_STACK: return "STACK";
    }
    return {};
}

std::string Segment::toString() const {
    ostringstream str;
    str << name << " ";
    switch (type) {
    case SEG_CODE:  str << "CODE "; break;
    case SEG_DATA:  str << "DATA "; break;
    case SEG_STACK: str << "STACK "; break;
    default:        str << "??? "; break; 
    }
    str << hexVal(address, false);
    return str.str();
}
