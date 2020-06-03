#include <algorithm>
#include <limits>
#include <cassert>
#include <set>
#include "memory.h"

MemoryRange::MemoryRange(Offset begin, Offset end) : begin(begin), end(end) {
    if (end < begin) throw MemoryError("Invalid range extents");
}

SegmentedAddress::SegmentedAddress(const Offset linear) {
    if (linear >= TOTALMEM_SIZE) throw MemoryError("Linear address too big while converting to segmented representation");
    segment = static_cast<Reg16>(linear >> 4);
    offset = static_cast<Reg16>(linear & 0xf);
}

void SegmentedAddress::normalize() {
    segment += offset >> 4;
    offset &= 0xf;
}

Memory::Memory() {
    //descriptors_.push_back(MapDescriptor(""))
    const Byte pattern[] = { 0xde, 0xad, 0xbe, 0xef };
    for (Offset i = 0, j = 0; i < TOTALMEM_SIZE; ++i) {
        // initialize memory data with initial pattern
        data_[i] = pattern[j];
        if (++j >= sizeof pattern) j = 0;
        // initialize memory map with id of top-level mapping
        map_[i] = 0;
    }
    descriptors_.emplace_back(MapDescriptor("Memory", { 0, TOTALMEM_SIZE - 1 }, 0, INVALID_MAPDESC_ID));
}

Byte Memory::read(const Offset addr) const {
    if (addr >= TOTALMEM_SIZE) throw MemoryError(std::string("Read outside memory bounds"));
    return data_[addr];
}

void Memory::write(const Offset addr, const Byte value) {
    if (addr >= TOTALMEM_SIZE) throw MemoryError(std::string("Read outside memory bounds"));
    data_[addr] = value;
}

void Memory::addMapping(const MemoryRange &range, const std::string &label) {
    if (range.begin >= TOTALMEM_SIZE || range.end >= TOTALMEM_SIZE)
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

const std::string &Memory::mapLabel(const Offset addr) const {
    if (addr >= TOTALMEM_SIZE) throw MemoryError("Mapping offset outside memory bounds");

    const auto idx = map_[addr];
    assert(idx < descriptors_.size());
    return descriptors_[idx].label;
}

