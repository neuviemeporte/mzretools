#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <cstdlib>

using Byte = uint8_t;
using Word = uint16_t;
using Dword = uint32_t;
using Size = size_t;
using Offset = size_t;

static constexpr Size PARAGRAPH_SIZE = 16;
constexpr Size operator "" _par(unsigned long long para) { return static_cast<Size>(para) * PARAGRAPH_SIZE; }
static constexpr Size PAGE_SIZE = 512;
constexpr Size operator "" _pg(unsigned long long pages) { return static_cast<Size>(pages) * PAGE_SIZE; }
static constexpr Size KB = 1024;
constexpr Size operator "" _kB(unsigned long long bytes) { return static_cast<Size>(bytes) * KB; }
static constexpr Size MB = 1024_kB;
constexpr Size operator "" _MB(unsigned long long bytes) { return static_cast<Size>(bytes) * MB; }
static constexpr Size MEM_TOTAL = 1_MB;
static constexpr Size PSP_SIZE = 0x100;



#endif // TYPES_H
