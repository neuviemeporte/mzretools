#ifndef ADDRESS_H
#define ADDRESS_H

#include <string>
#include "dos/types.h"

const int SEGMENT_SHIFT = 4;
inline Offset SEG_OFFSET(const Word seg) { return static_cast<Offset>(seg) << SEGMENT_SHIFT; }
inline Size BYTES_TO_PARA(const Size bytes) { return bytes / PARAGRAPH_SIZE + (bytes % PARAGRAPH_SIZE ? 1 : 0); }
inline const Word* WORD_PTR(const Byte* buf, const Offset off = 0) { return reinterpret_cast<const Word*>(buf + off); }

struct Address {
    Word segment, offset;
    Address(const Word segment, const Word offset) : segment(segment), offset(offset) {}
    Address(const Offset linear);
    Address() : Address(0, 0) {}
    operator std::string() const;
    std::string toString() const;

    inline Offset toLinear() const {
        return SEG_OFFSET(segment) + offset;
    }
    void normalize();
};
std::ostream& operator<<(std::ostream &os, const Address &arg);

#endif // ADDRESS_H