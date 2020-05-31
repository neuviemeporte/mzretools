#ifndef MEMORY_H
#define MEMORY_H

#include <cstdint>
#include <string>
#include <vector>

#include "types.h"
#include "debug.h"
#include "error.h"

struct MemoryRange {
    const Offset begin, end;
    MemoryRange(Offset begin, Offset end);
    bool operator==(const MemoryRange &arg) const { return arg.begin == begin && arg.end == end; }
};

class Memory {
private:
    // potentially need a descriptor for every byte in emulated memory, this should suffice
    using MapDescriptorIdx = uint32_t;

    static constexpr Size KB = 1024;
    static constexpr Size MB = 1024 * KB;
    static constexpr Size TOTAL_MEM = 1 * MB;
    static constexpr Size LOW_MEM = 64 * KB;
    static constexpr Size CONV_MEM  = 640 * KB;
    static constexpr Size UPPER_MEM = TOTAL_MEM - CONV_MEM;
    static constexpr MapDescriptorIdx INVALID_MAPDESC_ID = std::numeric_limits<MapDescriptorIdx>::max();

    struct MapDescriptor {
        const std::string label;
        const MemoryRange range;
        const MapDescriptorIdx idx, parent;

        MapDescriptor(const std::string &label, const MemoryRange &range, const MapDescriptorIdx idx, const MapDescriptorIdx parent) :
            label(label), range(range), idx(idx), parent(parent) {}
    };

    Byte data_[TOTAL_MEM];
    MapDescriptorIdx map_[TOTAL_MEM];
    std::vector<MapDescriptor> descriptors_;

public:
    Memory();

    Byte read(const Reg16 segment, const Reg16 offset);
    void write(const Reg16 segment, const Reg16 offset, const Byte value);
    void addMapping(const std::string &label, const MemoryRange &range);
    void dump(const std::string &path) const;

private:
    inline Offset toLinear(const Reg16 segment, const Reg16 offset) {
        return (static_cast<Offset>(segment) << 4) + offset;
    }

    MapDescriptorIdx addDescriptor(const std::string &label, const MemoryRange range, const MapDescriptorIdx parent);
};

#endif // MEMORY_H
