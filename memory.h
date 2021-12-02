#ifndef ADDRESS_H
#define ADDRESS_H

#include <ostream>
#include <array>

#include "types.h"

class MzImage;

static constexpr Size PARAGRAPH = 16;
constexpr Size operator "" _par(unsigned long long para) { return static_cast<Size>(para) * PARAGRAPH; }
static constexpr Size PAGE = 512;
constexpr Size operator "" _pg(unsigned long long pages) { return static_cast<Size>(pages) * PAGE; }
static constexpr Size KB = 1024;
constexpr Size operator "" _kB(unsigned long long bytes) { return static_cast<Size>(bytes) * KB; }
static constexpr Size MB = 1024_kB;
constexpr Size operator "" _MB(unsigned long long bytes) { return static_cast<Size>(bytes) * MB; }
static constexpr Size MEM_TOTAL = 1_MB;

struct MemoryRange {
    const Offset begin, end;
    MemoryRange(Offset begin, Offset end);
    Size size() const { return end - begin + 1; }
    bool operator==(const MemoryRange &arg) const { return arg.begin == begin && arg.end == end; }
    bool contains(const Offset addr) const { return addr >= begin && addr <= end; }
};

struct SegmentedAddress {
    Word segment, offset;
    SegmentedAddress(const Word segment, const Word offset) : segment(segment), offset(offset) {}
    SegmentedAddress(const Offset linear);
    SegmentedAddress() : SegmentedAddress(0, 0) {}
    inline Offset toLinear() const {
        return (static_cast<Offset>(segment) << 4) + offset;
    }
    void normalize();
};

std::ostream& operator<<(std::ostream &os, const SegmentedAddress &arg);


class Arena {
private:
    static constexpr Offset INIT_BREAK = 0x7e00; // beginning of free conventional memory block past the DOS bootsector

private:
    std::array<Byte, MEM_TOTAL> data_;
    Offset break_;

public:
    Arena();

    Byte read(const Offset addr) const;
    void write(const Offset addr, const Byte value);
    void write(const Offset addr, const Word value);
    auto pointer(const Offset addr) { return data_.begin() + addr; }
    auto pointer(const Offset addr) const { return data_.cbegin() + addr; }
    void dump(const std::string &path) const;
};

#endif // ADDRESS_H