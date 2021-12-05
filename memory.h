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

/*Real mode memory map:
--- 640 KiB RAM ("Low memory") 
0x00000 - 0x003FF: (1 KiB): Real Mode IVT (Interrupt Vector Table)
0x00400 - 0x004EF: (256 B): BDA (BIOS data area)
0x004F0 - 0x004FF: (16B) inter-application communications area
0x00500 - 0x07BFF: (29 KiB): Conventional memory, usable memory
0x07C00 - 0x07DFF: (512 B): OS BootSector
0x07E00 - 0x7FFFF: (480.5 KiB): Conventional memory
0x80000 - 0x9FFFF: (128 KiB): EBDA (Extended BIOS Data Area) 
--- 384 KiB System / Reserved / UMA ("Upper Memory")
0xA0000 - 0xBFFFF: (128 KiB): Video display memory, hardware mapped 
--- ROM and hardware mapped / Shadow RAM
0xC0000 - 0xC7FFF: (32 KiB): (typically) Video BIOS
0xC8000 - 0xEFFFF: (160 KiB): (typically) BIOS Expansions, addon card ROMs
0xF0000 - 0xFFFFF: (64 KiB): Motherboard BIOS
--- extended memory 
--- HMA
0x100000 - 0x10FFEF: (64KiB - 16)
--- extended memory available from protected mode only
*/
class Arena {
private:
    static constexpr Offset INIT_BREAK = 0x7e00; // beginning of free conventional memory block past the DOS bootsector
    static constexpr Offset MEM_END = 0x80000; // end of usable memory, start of EBDA

private:
    std::array<Byte, MEM_TOTAL> data_;
    Offset break_;

public:
    Arena();
    Size free() const { return MEM_END - break_; }
    Offset freeStart() const { return break_; }
    Offset freeEnd() const { return MEM_END; }

    Byte read(const Offset addr) const;
    void write(const Offset addr, const Byte value);
    void write(const Offset addr, const Word value);
    auto pointer(const Offset addr) { return data_.begin() + addr; }
    auto pointer(const Offset addr) const { return data_.cbegin() + addr; }
    void dump(const std::string &path) const;
};

#endif // ADDRESS_H