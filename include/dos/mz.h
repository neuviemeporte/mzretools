#ifndef MZ_H
#define MZ_H

#include <cstdint>
#include <string>
#include <vector>
#include <ostream>
#include "dos/types.h"
#include "dos/address.h"

constexpr Size MAX_COMFILE_SIZE = 0xff00;

class MzImage {
private:
    static constexpr Size HEADER_SIZE = 14 * sizeof(Word);
    static constexpr Size RELOC_SIZE = 2 * sizeof(Word);
    static constexpr Word SIGNATURE = 0x5A4D;

#pragma pack(push, 1)
    struct Header {
        Word signature;            // 0x00, == 0x5A4D ("MZ")
        Word last_page_size;       // 0x02, any remainder < 512 in the last page
        Word pages_in_file;        // 0x04, each page is 512 bytes
        Word num_relocs;           // 0x06, size of relocation table
        Word header_paragraphs;    // 0x08, length of header, indicates offset of load module within the exe
        Word min_extra_paragraphs; // 0x0a, minimum required memory
        Word max_extra_paragraphs; // 0x0c, max program would like
        Word ss;                   // 0x0e, paragraph addr of stack relative to load module
        Word sp;                   // 0x10, does not need to be relocated
        Word checksum;             // 0x12, rarely used
        Word ip;                   // 0x14, does not need to be relocated
        Word cs;                   // 0x16, relocated at load time by adding by adding addr of start segment to it
        Word reloc_table_offset;   // 0x18, from start of file
        Word overlay_number;       // 0x1a, rarely used
    } header_;
    static_assert(sizeof(Header) == HEADER_SIZE, "Invalid EXE header size");
#pragma pack(pop)

    struct Relocation {
        Word offset;
        Word segment;
        Word value;
        Relocation() : offset(0), segment(0), value(0) {}
    };

    const std::string path_;
    Size filesize_, loadModuleSize_;
    std::vector<Relocation> relocs_;
    std::vector<Byte> ovlinfo_;
    Offset loadModuleOffset_;
    Address entrypoint_;
    Byte *loadModuleData_; // TODO: vector

public:
    MzImage(const std::string &path);
    ~MzImage();
    const std::string& path() const { return path_; }
    std::string dump() const;
    Size loadModuleSize() const { return loadModuleSize_; }
    Offset loadModuleOffset() const { return loadModuleOffset_; }
    const Byte* loadModuleData() const { return loadModuleData_; }
    Size minAlloc() const { return header_.min_extra_paragraphs * PARAGRAPH_SIZE; }
    Size maxAlloc() const { return header_.max_extra_paragraphs * PARAGRAPH_SIZE; }
    Address codeAddress() const { return Address(header_.cs, header_.ip); }
    Address stackAddress() const { return Address(header_.ss, header_.sp); }
    void load(const Word loadSegment);
};

#endif // MZ_H