#include <algorithm>
#include <limits>
#include <cassert>
#include <set>
#include "memory.h"

MemoryRange::MemoryRange(Offset begin, Offset end) : begin(begin), end(end) {
    if (end > begin) throw MemoryError("Invalid range extents");
}

Memory::Memory() {
    //descriptors_.push_back(MapDescriptor(""))
    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
    for (Offset i = 0, j = 0; i < TOTAL_MEM; ++i) {
        // initialize memory data with initial pattern
        data_[i] = pattern[j];
        if (j++ > sizeof pattern) j = 0;
        // initialize memory map with id of top-level mapping
        map_[i] = 0;
    }
    descriptors_.emplace_back(MapDescriptor("Memory", { 0, TOTAL_MEM - 1 }, 0, INVALID_MAPDESC_ID));
}

Byte Memory::read(const Reg16 segment, const Reg16 offset) {
    const size_t addr = toLinear(segment, offset);

#ifdef MEM_DEBUG
    if (addr >= TOTAL_MEM) throw MemoryError(std::string("Read outside memory bounds"));
#endif

    return data_[addr];
}

void Memory::write(const Reg16 segment, const Reg16 offset, const Byte value) {
    const size_t addr = toLinear(segment, offset);

#ifdef MEM_DEBUG
    if (addr >= TOTAL_MEM) throw MemoryError(std::string("Read outside memory bounds"));
#endif

    data_[addr] = value;
}

void Memory::addMapping(const std::string &label, const MemoryRange &range) {
    if (range.begin >= TOTAL_MEM || range.end >= TOTAL_MEM)
        throw MemoryError("Mapping range outside memory bounds");

    // expect to have at least the top-level global descriptor present
    assert(descriptors_.size() > 0);
    // initialize work variables
    MapDescriptorIdx previous = INVALID_MAPDESC_ID;
    MapDescriptorIdx child, current;
    // Iterate over existing descriptors in memory map, create child descriptor(s)
    // and overwrite map entries to point to them.
    // Our new mapping can result in multiple child descriptors being created
    // if the range being mapped covers more than one underlying parent mapping.
    for (Offset i = range.begin; i <= range.end; ++i) {
        current = map_[i];
        // found different id in map, create a child mapping for it or find a preexisting one
        if (current != previous) {
            child = addDescriptor(label, range, current);
            previous = current;
        }
        // overwrite entry in map with id of child descriptor
        map_[i] = child;
    }
}

Memory::MapDescriptorIdx Memory::addDescriptor(const std::string &label, const MemoryRange range, const MapDescriptorIdx parent) {
    // if descriptor spanning the same range and having the same parent already exists, return its index and don't add anything
    auto found = std::find_if(descriptors_.begin(), descriptors_.end(), [&](const MapDescriptor &item) {
        return item.range == range && item.parent == parent;
    });
    if (found != descriptors_.end()) {
        return found->idx;
    }

    // otherwise, add descriptor under new idx
    auto newIdx = static_cast<MapDescriptorIdx>(descriptors_.size());
    if (newIdx >= INVALID_MAPDESC_ID)
        throw MemoryError("Out of map descriptor slots");

    descriptors_.emplace_back(MapDescriptor(label, range, newIdx, parent));
    return newIdx;
}


