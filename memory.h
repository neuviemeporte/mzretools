#ifndef MEMORY_H
#define MEMORY_H

#include <cstdint>
#include <string>
#include <vector>

#include "types.h"
#include "debug.h"
#include "error.h"

static constexpr Size KB = 1024;
static constexpr Size MB = 1024 * KB;
static constexpr Size PARAGRAPH_SIZE = 16;
static constexpr Size TOTALMEM_SIZE = 1 * MB;

struct MemoryRange {
    const Offset begin, end;
    MemoryRange(Offset begin, Offset end);
    Size size() const { return end - begin + 1; }
    bool operator==(const MemoryRange &arg) const { return arg.begin == begin && arg.end == end; }
    bool contains(const Offset addr) const { return addr >= begin && addr <= end; }
};

struct SegmentedAddress {
    Reg16 segment, offset;
    SegmentedAddress(const Reg16 segment, const Reg16 offset) : segment(segment), offset(offset) {}
    SegmentedAddress(const Offset linear);
    inline Offset toLinear() const {
        return (static_cast<Offset>(segment) << 4) + offset;
    }
    void normalize();
};

class Memory {
private:
    // potentially need a descriptor for every byte in emulated memory, this should suffice
    using MapDescriptorIdx = uint32_t;
    static constexpr MapDescriptorIdx INVALID_MAPDESC_ID = std::numeric_limits<MapDescriptorIdx>::max();

    struct MapDescriptor {
        const std::string label;
        const MemoryRange range;
        const MapDescriptorIdx idx, parent;

        MapDescriptor(const std::string &label, const MemoryRange &range, const MapDescriptorIdx idx, const MapDescriptorIdx parent) :
            label(label), range(range), idx(idx), parent(parent) {}
    };

    Byte data_[TOTALMEM_SIZE];
    MapDescriptorIdx map_[TOTALMEM_SIZE];
    std::vector<MapDescriptor> descriptors_;

public:
    Memory();

    Size size() const { return TOTALMEM_SIZE; }

    Byte read(const Offset addr) const;
    void write(const Offset addr, const Byte value);

    void addMapping(const MemoryRange &range, const std::string &label);
    const std::string& mapLabel(const Offset addr) const;
    void dump(const std::string &path) const;

private:

    MapDescriptorIdx addDescriptor(const std::string &label, const MemoryRange range, const MapDescriptorIdx parent);
};

#endif // MEMORY_H
