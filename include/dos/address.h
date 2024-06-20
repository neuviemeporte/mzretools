#ifndef ADDRESS_H
#define ADDRESS_H

#include <string>
#include <cassert>
#include <regex>

#include "dos/types.h"

static constexpr Size MEM_TOTAL = 1_MB;
static constexpr Size SEGMENT_SIZE = 64_kB;
static constexpr int SEGMENT_MASK = 0xf0000;
// amount of right shift needed to convert linear offset values into segment values (losing the 16 byte aliasing factor), or left shift for the opposite (segment to linear)
static constexpr int SEGMENT_SHIFT = 4; 
// amount of shift needed to obtain the count of full, 64k size segments covered by a linear offset
static constexpr int SEGMENT_SIZE_SHIFT = 16;
static constexpr Offset OFFSET_MASK = 0xffff;
// masking to apply to obtain the normalized offset value (the aliasing factor)
static constexpr Offset OFFSET_NORMAL_MASK = 0xf;
static constexpr int OFFSET_STRLEN = 6; // 6 hex digits for linear offsets up to 0x100000
static constexpr int WORD_STRLEN = 4;   // 4 hex digits for segment values up to 0xffff
static constexpr Word ADDR_INVALID = 0xffff;
static constexpr Offset OFFSET_MAX = 0xffff;

inline Offset SEG_TO_OFFSET(const Word seg) { return static_cast<Offset>(seg) << SEGMENT_SHIFT; }
inline Word OFFSET_TO_SEG(const Offset off) { return static_cast<Word>(off >> SEGMENT_SHIFT); }
inline Size BYTES_TO_PARA(const Size bytes) { return bytes / PARAGRAPH_SIZE + (bytes % PARAGRAPH_SIZE ? 1 : 0); }
inline const Word* WORD_PTR(const Byte* buf, const Offset off = 0) { return reinterpret_cast<const Word*>(buf + off); }
inline Word DWORD_SEGMENT(const DWord val) { return val >> 16; }
inline Word DWORD_OFFSET(const DWord val) { return val & 0xffff; }
inline bool linearPastSegment(const Offset linear, const Word segment) { return linear >= SEG_TO_OFFSET(segment); }
inline bool linearInSegment(const Offset linear, const Word segment) { return linearPastSegment(linear, segment) && linear - SEG_TO_OFFSET(segment) <= OFFSET_MAX; }

// A segmented address
struct Address {
    Word segment, offset;
    Address(const Word segment, const Word offset) : segment(segment), offset(offset) {}
    Address(const Offset linear);
    Address(const std::string &str, const bool fixNormal = false);
    Address(const Address &other, const SWord displacement);
    Address() : Address(ADDR_INVALID, ADDR_INVALID) {}

    bool operator==(const Address &arg) const { return toLinear() == arg.toLinear(); }
    bool operator!=(const Address &arg) const { return !(*this == arg); }
    bool operator<(const Address &arg) const { return toLinear() < arg.toLinear(); }
    bool operator>(const Address &arg) const { return toLinear() > arg.toLinear(); }
    bool operator<=(const Address &arg) const { return toLinear() <= arg.toLinear(); }
    bool operator>=(const Address &arg) const { return toLinear() >= arg.toLinear(); }
    Address operator+(const SByte arg) const { return {segment, static_cast<Word>(offset + arg)}; }
    Address operator+(const SWord arg) const { return {segment, static_cast<Word>(offset + arg)}; }
    Address operator+(const DWord arg) const;
    Address operator+(const Offset arg) const { return {segment, static_cast<Word>(offset + arg)}; }
    Size operator-(const Address &arg) const { return toLinear() - arg.toLinear(); }
    Address operator-(const Offset &arg) const { return {segment, static_cast<Word>(offset - arg)}; }
    Address& operator+=(const SWord adjust) { offset += adjust; return *this; }
    Address& operator+=(const SByte adjust) { offset += adjust; return *this; }
    Address& operator+=(const Byte adjust) { offset += adjust; return *this; }

    void set(const Offset linear);
    std::string toString(const bool brief = false) const;
    inline Offset toLinear() const { return SEG_TO_OFFSET(segment) + offset; }
    bool isNull() const { return segment == 0 && offset == 0; }
    bool isValid() const { return segment != ADDR_INVALID || offset != ADDR_INVALID; }
    bool inSegment(const Word arg) const { return toLinear() >= SEG_TO_OFFSET(arg) && toLinear() - SEG_TO_OFFSET(arg) <= OFFSET_MAX; }
    void normalize();
    void relocate(const Word reloc);
    void rebase(const Word base);
    void move(const Word segment);
};
std::ostream& operator<<(std::ostream &os, const Address &arg);
inline std::string operator+(const std::string &str, const Address &arg) { return str + arg.toString(); }

// An address range, both ends inclusive
struct Block {
    Address begin, end;
    Block(const Address &begin, const Address &end) : begin(begin), end(end) {}
    Block(const Address &begin) : Block(begin, begin) {}
    Block(const Offset begin, const Offset end) : Block(Address{begin}, Address{end}) {}
    Block(const Offset begin) : Block(Address(begin)) {}
    Block(const std::string &blockStr);
    Block(const std::string &from, const std::string &to);
    Block() : Block(MEM_TOTAL - 1, 0) {} // invalid block

    bool operator==(const Block &arg) const { return begin == arg.begin && end == arg.end; }
    bool operator<(const Block &arg) const { return begin < arg.begin; }

    std::string toString(const bool linear = false, const bool showSize = true) const;
    std::string toHex() const;
    Size size() const { return (isValid() ? (end - begin) + 1 : 0); }
    bool isValid() const { return begin.isValid() && end.isValid() && begin <= end; }
    bool inSegment(const Word seg) const { return begin.inSegment(seg) && end.inSegment(seg); }
    bool contains(const Address &addr) const { return addr >= begin && addr <= end; }
    bool intersects(const Block &other) const;
    bool adjacent(const Block &other) const;

    void relocate(const Word reloc) { begin.relocate(reloc); end.relocate(reloc); }
    void rebase(const Word base) { begin.rebase(base); end.rebase(base); }
    void move(const Word seg) { begin.move(seg); end.move(seg); }
    void coalesce(const Block &other);
    Block coalesce(const Block &other) const;
};
std::ostream& operator<<(std::ostream &os, const Block &arg);
inline std::string operator+(const std::string &str, const Block &arg) { return str + arg.toString(); }

struct Segment {
    std::string name;
    enum Type {
        SEG_NONE,
        SEG_CODE,
        SEG_DATA,
        SEG_STACK
    } type;
    Word address;

    static std::smatch stringMatch(const std::string &str);

    Segment(const std::string &name, Type type, Word address) : name(name), type(type), address(address) {}
    Segment(const std::smatch &match);
    Segment(const std::string &str) : Segment(stringMatch(str)) {}
    Segment() : Segment("", SEG_NONE, 0) {}

    bool operator==(const Segment &other) const { return type != SEG_NONE && type == other.type && address == other.address; }
    bool operator!=(const Segment &other) const { return !(*this == other); }
    bool operator<(const Segment &other) const { return address < other.address; }
    std::string toString() const;
};

#endif // ADDRESS_H