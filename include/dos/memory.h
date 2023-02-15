#ifndef MEMORY_H
#define MEMORY_H

#include <ostream>
#include <array>
#include "dos/types.h"
#include "dos/address.h"

// TODO: 
// - implement MCBs

/*Real mode memory map:
--- 640 KiB RAM ("Low memory") 
0x00000 - 0x003FF: (1 KiB): Real Mode IVT (Interrupt Vector Table)
0x00400 - 0x004EF: (256 B): BDA (BIOS data area)
0x004F0 - 0x004FF: (16B) inter-application communications area
0x00500 - 0x07BFF: (29 KiB): Conventional memory, usable memory
0x07C00 - 0x07DFF: (512 B): OS BootSector
0x07E00 - 0x7FFFF: (480.5 KiB): Conventional memory
0x80000 - 0x9FFFF: (128 KiB): EBDA (Extended BIOS Data Area), actual size varies, obtainable through int12
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
class Memory {
private:
    static constexpr Offset INIT_BREAK = 0x500; // beginning of free conventional memory block
    static constexpr Offset MEM_END = 0xa0000; // end of usable memory, start of UMA

private:
    std::array<Byte, MEM_TOTAL> data_;
    Offset break_;

public:
    Memory();
    Memory(const Word segment, const Byte *data, const Size size);
    Size size() const { return MEM_TOTAL; }
    Size availableBlock() const { return BYTES_TO_PARA(MEM_END - break_); }
    Size availableBytes() const { return availableBlock() * PARAGRAPH_SIZE; }
    Offset freeStart() const { return break_; }
    Offset freeEnd() const { return MEM_END; }

    void allocBlock(const Size para);
    void freeBlock(const Size para);
    Byte readByte(const Offset addr) const;
    Word readWord(const Offset addr) const;
    Byte readByte(const Address addr) const { return readByte(addr.toLinear()); }
    Word readWord(const Address addr) const { return readWord(addr.toLinear()); }
    void writeByte(const Offset addr, const Byte value);
    void writeWord(const Offset addr, const Word value);
    void writeBuf(const Offset addr, const Byte *data, const Size size);
    const Byte* pointer(const Offset addr) const { return data_.cbegin() + addr; }
    const Byte* pointer(const Address &addr) const { return data_.cbegin() + addr.toLinear(); }
    const Byte* base() const { return pointer(0); }

    std::string info() const;
    void dump(const Block &range, const std::string &path) const;
};

#endif // MEMORY_H