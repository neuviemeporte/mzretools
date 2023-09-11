#ifndef ROUTINE_H
#define ROUTINE_H

#include <string>
#include <vector>

#include "dos/types.h"
#include "dos/address.h"

using RoutineId = int;

class ScanQueue;

struct RoutineEntrypoint {
    Address addr;
    RoutineId id;
    RoutineEntrypoint(const Address &addr, const RoutineId id) : addr(addr), id(id) {}
    bool operator==(const Address &arg) const { return addr == arg; }
};

struct Routine {
    std::string name;
    Block extents; // largest contiguous block starting at routine entrypoint, may contain unreachable regions
    std::vector<Block> reachable, unreachable;
    bool near;

    Routine() : near(true) {}
    Routine(const std::string &name, const Block &extents) : name(name), extents(extents), near(true) {}
    Address entrypoint() const { return extents.begin; }
    // for sorting purposes
    bool operator<(const Routine &other) { return entrypoint() < other.entrypoint(); }
    bool isValid() const { return extents.isValid(); }
    bool isUnchunked() const { return reachable.size() == 1 && unreachable.empty() && reachable.front() == extents; }
    bool isReachable(const Block &b) const;
    bool isUnreachable(const Block &b) const;
    Block mainBlock() const;
    Block blockContaining(const Address &a) const;
    Block nextReachable(const Address &from) const;
    bool colides(const Block &block, const bool checkExtents = true) const;
    std::string toString(const bool showChunks = true) const;
    std::vector<Block> sortedBlocks() const;
};

// A map of an executable, records which areas have been claimed by routines, and which have not, serializable to a file
class RoutineMap {
    friend class AnalysisTest;
    Size codeSize;
    std::vector<Routine> routines;
    std::vector<Block> unclaimed;
    std::vector<Segment> segments;
    // TODO: turn these into a context struct, pass around instead of members
    RoutineId curId, prevId, curBlockId, prevBlockId;

public:
    RoutineMap() {}
    RoutineMap(const ScanQueue &sq, const Word loadSegment, const Size codeSize);
    RoutineMap(const std::string &path, const Word reloc = 0);

    Size size() const { return routines.size(); }
    Routine getRoutine(const Size idx) const { return routines.at(idx); }
    Routine getRoutine(const Address &addr) const;
    bool empty() const { return routines.empty(); }
    Size match(const RoutineMap &other) const;
    Routine colidesBlock(const Block &b) const;
    void save(const std::string &path, const Word reloc, const bool overwrite = false) const;
    std::string dump() const;
    void setSegments(const std::vector<Segment> &seg);
    const auto& getSegments() const { return segments; }
    Size segmentCount(const Segment::Type type) const;
    Segment findSegment(const Word addr) const;
    Segment findSegment(const std::string &name) const;
    
private:
    void closeBlock(Block &b, const Offset off, const ScanQueue &sq, const Word reloc);
    Block moveBlock(const Block &b, const Word segment) const;
    void sort();
    void loadFromMapFile(const std::string &path, const Word reloc);
    void loadFromIdaFile(const std::string &path, const Word reloc);
};

#endif // ROUTINE_H
