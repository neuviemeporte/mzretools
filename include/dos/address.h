#ifndef ADDRESS_H
#define ADDRESS_H

#include <string>
#include <cassert>
#include "dos/types.h"

static constexpr Size MEM_TOTAL = 1_MB;
static constexpr int SEGMENT_SHIFT = 4;
static constexpr int OFFSET_MASK = 0xf;
static constexpr int OFFSET_STRLEN = MEM_TOTAL % 10;
static constexpr int WORD_STRLEN = UINT16_MAX % 10;
static constexpr Word ADDR_INVALID = 0xffff;

inline Offset SEG_OFFSET(const Word seg) { return static_cast<Offset>(seg) << SEGMENT_SHIFT; }
inline Size BYTES_TO_PARA(const Size bytes) { return bytes / PARAGRAPH_SIZE + (bytes % PARAGRAPH_SIZE ? 1 : 0); }
inline const Word* WORD_PTR(const Byte* buf, const Offset off = 0) { return reinterpret_cast<const Word*>(buf + off); }

struct Address {
    Word segment, offset;
    Address(const Word segment, const Word offset) : segment(segment), offset(offset) {}
    Address(const Offset linear);
    Address(const std::string &str);
    Address(const Address &other, const SWord displacement);
    Address() : Address(ADDR_INVALID, ADDR_INVALID) {}

    bool operator==(const Address &arg) const { return toLinear() == arg.toLinear(); }
    bool operator!=(const Address &arg) const { return !(*this == arg); }
    bool operator<=(const Address &arg) const { return toLinear() <= arg.toLinear(); }
    bool operator>=(const Address &arg) const { return toLinear() >= arg.toLinear(); }
    // TODO: handle overflow
    Address operator+(const SByte arg) const { return {segment, static_cast<Word>(offset + arg)}; }
    Address operator+(const SWord arg) const { return {segment, static_cast<Word>(offset + arg)}; }
    Address operator+(const DWord arg) const;
    Address operator+(const size_t arg) const { return {segment, static_cast<Word>(offset + arg)}; }
    Size operator-(const Address &arg) const { return toLinear() - arg.toLinear(); }
    Address& operator+=(const SWord adjust) { offset += adjust; return *this; }
    Address& operator+=(const SByte adjust) { offset += adjust; return *this; }
    Address& operator+=(const Byte adjust) { offset += adjust; return *this; }

    void set(const Offset linear);
    std::string toString(const bool brief = false) const;
    inline Offset toLinear() const { return SEG_OFFSET(segment) + offset; }
    bool isNull() const { return segment == 0 && offset == 0; }
    bool isValid() const { return segment != ADDR_INVALID || offset != ADDR_INVALID; }
    void normalize();
    void relocate(const SWord reloc) { segment += reloc; }
    void rebase(const Word base);
};
std::ostream& operator<<(std::ostream &os, const Address &arg);
inline std::string operator+(const std::string &str, const Address &arg) { return str + arg.toString(); }

struct Block {
    Address begin, end;
    Block(const Address &begin, const Address &end) : begin(begin), end(end) {}
    Block(const Address &begin) : Block(begin, begin) {}
    Block(const Offset begin, const Offset end) : Block(Address{begin}, Address{end}) {}
    Block(const std::string &from, const std::string &to);
    Block() : Block(MEM_TOTAL - 1, 0) {} // null block

    bool operator==(const Block &arg) const { return begin == arg.begin && end == arg.end; }

    std::string toString(const bool linear = false) const;
    Size size() const { assert(isValid()); return end - begin; }
    bool isValid() const { return begin <= end; }
    bool contains(const Address &addr) const { return addr.toLinear() >= begin.toLinear() && addr.toLinear() <= end.toLinear(); }
    bool intersects(const Block &other) const;

    void relocate(const SWord reloc) { begin.relocate(reloc); end.relocate(reloc); }
    void rebase(const Word base) { begin.rebase(base); end.rebase(base); }
    Block coalesce(const Block &other) const;
};
std::ostream& operator<<(std::ostream &os, const Block &arg);
inline std::string operator+(const std::string &str, const Block &arg) { return str + arg.toString(); }

#endif // ADDRESS_H