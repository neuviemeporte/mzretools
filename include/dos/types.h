#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <cstdlib>

using Byte   = uint8_t;
using SByte  = int8_t;
using Word   = uint16_t;
using SWord  = int16_t;
using DWord  = uint32_t;
using Size   = size_t;
// for representing linear addresses from the memory base, required because DOS addresses don't fit in a 16-bit word
using Offset = size_t;

template<typename T> SByte BYTE_SIGNED(const T val) { return static_cast<SByte>(val); }
template<typename T> SWord WORD_SIGNED(const T val) { return static_cast<SWord>(val); }

static constexpr Size PARAGRAPH_SIZE = 16;
constexpr Size operator "" _par(unsigned long long para) { return static_cast<Size>(para) * PARAGRAPH_SIZE; }
static constexpr Size PAGE_SIZE = 512;
constexpr Size operator "" _pg(unsigned long long pages) { return static_cast<Size>(pages) * PAGE_SIZE; }
static constexpr Size KB = 1024;
constexpr Size operator "" _kB(unsigned long long bytes) { return static_cast<Size>(bytes) * KB; }
static constexpr Size MB = 1024_kB;
constexpr Size operator "" _MB(unsigned long long bytes) { return static_cast<Size>(bytes) * MB; }


#endif // TYPES_H
