#ifndef MZ_H
#define MZ_H

#include <cstdint>
#include <string>
#include <vector>
#include <ostream>
#include "dos/types.h"
#include "dos/address.h"

static constexpr Size MAX_COMFILE_SIZE = 0xff00;
static constexpr Size MZ_HEADER_SIZE = 14 * sizeof(Word);
static constexpr Size MZ_RELOC_SIZE = 2 * sizeof(Word);
static constexpr Word MZ_SIGNATURE = 0x5A4D;

class MzImage {
private:

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
        Header() : signature(0), last_page_size(0), pages_in_file(0), num_relocs(0), header_paragraphs(0), min_extra_paragraphs(0), max_extra_paragraphs(0), 
            ss(0), sp(0), checksum(0), ip(0), cs(0), reloc_table_offset(0), overlay_number(0) {} 
    } header_;
    static_assert(sizeof(Header) == MZ_HEADER_SIZE, "Invalid EXE header size");
#pragma pack(pop)

    struct Relocation {
        Word offset;
        Word segment;
        Word value;
        Relocation() : offset(0), segment(0), value(0) {}
    };

    const std::string path_;
    Size filesize_, loadModuleSize_;
    std::vector<Byte> loadModuleData_, ovlinfo_;
    std::vector<Relocation> relocs_;
    Offset loadModuleOffset_;
    Address entrypoint_;
    Word loadSegment_;

public:
    MzImage(const std::string &path);
    // useful for testing
    MzImage(const std::vector<Byte> &code); 
    MzImage(const MzImage &other) = delete;
    const std::string& path() const { return path_; }
    std::string dump() const;
    Size headerLength() const { return header_.header_paragraphs * PARAGRAPH_SIZE; }
    Size loadModuleSize() const { return loadModuleSize_; }
    Offset loadModuleOffset() const { return loadModuleOffset_; }
    const Byte* loadModuleData() const { return loadModuleData_.data(); }
    Word loadSegment() const { return loadSegment_; }
    Size minAlloc() const { return header_.min_extra_paragraphs * PARAGRAPH_SIZE; }
    Size maxAlloc() const { return header_.max_extra_paragraphs * PARAGRAPH_SIZE; }
    // TODO: should be relocated (i.e. apply loadSegment_)? 
    Address entrypoint() const { return Address(header_.cs, header_.ip); }
    Address stackPointer() const { return Address(header_.ss, header_.sp); }
    void load(const Word loadSegment);
};

#endif // MZ_H