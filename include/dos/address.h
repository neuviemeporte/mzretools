#ifndef ADDRESS_H
#define ADDRESS_H

#include <string>
#include "dos/types.h"

static constexpr Size MEM_TOTAL = 1_MB;
static const int SEGMENT_SHIFT = 4;

inline Offset SEG_OFFSET(const Word seg) { return static_cast<Offset>(seg) << SEGMENT_SHIFT; }
inline Size BYTES_TO_PARA(const Size bytes) { return bytes / PARAGRAPH_SIZE + (bytes % PARAGRAPH_SIZE ? 1 : 0); }
inline const Word* WORD_PTR(const Byte* buf, const Offset off = 0) { return reinterpret_cast<const Word*>(buf + off); }

struct Address {
    Word segment, offset;
    Address(const Word segment, const Word offset) : segment(segment), offset(offset) {}
    Address(const Offset linear);
    Address() : Address(0, 0) {}

    bool operator==(const Address &arg) const { return toLinear() == arg.toLinear(); }
    bool operator<=(const Address &arg) const { return toLinear() <= arg.toLinear(); }

    std::string toString() const;
    inline Offset toLinear() const { return SEG_OFFSET(segment) + offset; }
    bool isNull() const { return segment == 0 && offset == 0; }
    void normalize();
};
std::ostream& operator<<(std::ostream &os, const Address &arg);
inline std::string operator+(const std::string &str, const Address &arg) { return str + arg.toString(); }

struct Block {
    Address begin, end;
    Block(const Address &begin, const Address &end) : begin(begin), end(end) {}
    Block(const Address &begin) : Block(begin, begin) {}
    Block(const Offset begin, const Offset end) : Block(Address{begin}, Address{end}) {}
    Block() : Block(MEM_TOTAL - 1, 0) {}

    bool operator==(const Block &arg) const { return begin == arg.begin && end == arg.end; }

    std::string toString() const;
    bool isValid() const { return begin <= end; }
    bool contains(const Address &addr) const { return addr.toLinear() >= begin.toLinear() && addr.toLinear() <= end.toLinear(); }
    bool intersects(const Block &other) const;
    Block coalesce(const Block &other) const;
};
std::ostream& operator<<(std::ostream &os, const Block &arg);
inline std::string operator+(const std::string &str, const Block &arg) { return str + arg.toString(); }

#endif // ADDRESS_H