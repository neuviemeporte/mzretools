#ifndef SEGMENT_H
#define SEGMENT_H

#include <string>
#include "dos/types.h"

struct Segment {
    enum Type {
        SEG_NONE,
        SEG_CODE,
        SEG_DATA,
        SEG_STACK
    } type;
    Word value;
    Segment(Type type, Word value) : type(type), value(value) {}
    Segment(const std::string &str);
    bool operator==(const Segment &other) const { return type == other.type && value == other.value; }
    std::string toString() const;
};

#endif // SEGMENT_H
