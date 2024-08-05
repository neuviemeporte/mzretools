#ifndef ROUTINE_H
#define ROUTINE_H

#include <string>
#include <vector>

#include "dos/types.h"
#include "dos/address.h"

using RoutineId = int;
static constexpr RoutineId NULL_ROUTINE = 0;

class ScanQueue;

struct RoutineEntrypoint {
    Address addr;
    RoutineId id;
    bool near;
    std::string name;

    RoutineEntrypoint(const Address &addr, const RoutineId id, const bool near = true) : addr(addr), id(id), near(near) {}
    RoutineEntrypoint() : RoutineEntrypoint({}, NULL_ROUTINE, false) {}
    bool operator==(const Address &arg) const { return addr == arg; }
    std::string toString() const;
};

struct Routine {
    std::string name;
    Block extents; // largest contiguous block starting at routine entrypoint, may contain unreachable regions
    std::vector<Block> reachable, unreachable;
    RoutineId id;
    bool near, ignore, complete, unclaimed, external, detached, assembly;

    Routine(const std::string &name, const Block &extents) : name(name), extents(extents), id(NULL_ROUTINE),
        near(true), ignore(false), complete(false), unclaimed(false), external(false), detached(false), assembly(false) {}
    Routine() : Routine("", {}) {}
    Address entrypoint() const { return extents.begin; }
    Size size() const { return extents.size(); }
    Size reachableSize() const;
    Size unreachableSize() const;
    // for sorting purposes
    bool operator<(const Routine &other) const { return entrypoint() < other.entrypoint(); }
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
    void recalculateExtents();
};

// A map of an executable, records which areas have been claimed by routines, and which have not, serializable to a file
class RoutineMap {
    friend class AnalysisTest;
    Size mapSize;
    std::vector<Routine> routines;
    std::vector<Block> unclaimed;
    std::vector<Segment> segments;
    // TODO: turn these into a context struct, pass around instead of members
    RoutineId curId, prevId, curBlockId, prevBlockId;

public:
    RoutineMap(const Size mapSize) : mapSize(mapSize), curId(0), prevId(0), curBlockId(0), prevBlockId(0) {}
    RoutineMap(const ScanQueue &sq, const std::vector<Segment> &segs, const Word loadSegment, const Size mapSize);
    RoutineMap(const std::string &path, const Word reloc = 0);
    RoutineMap() : RoutineMap(0) {}

    Size routineCount() const { return routines.size(); }
    Routine getRoutine(const Size idx) const { return routines.at(idx); }
    Routine getRoutine(const Address &addr) const;
    Routine getRoutine(const std::string &name) const;
    Routine& getMutableRoutine(const std::string &name);
    Routine findByEntrypoint(const Address &ep) const;
    bool empty() const { return routines.empty(); }
    Size match(const RoutineMap &other) const;
    Routine colidesBlock(const Block &b) const;
    void order();
    void save(const std::string &path, const Word reloc, const bool overwrite = false) const;
    std::string dump(const bool verbose = true, const bool hide = false, const bool format = false) const;
    const auto& getSegments() const { return segments; }
    Size segmentCount(const Segment::Type type) const;
    Segment findSegment(const Word addr) const;
    Segment findSegment(const std::string &name) const;
    Segment findSegment(const Offset off) const;
    void setSegments(const std::vector<Segment> &seg);
    
private:
    void closeBlock(Block &b, const Address &next, const ScanQueue &sq);
    Block moveBlock(const Block &b, const Word segment) const;
    void sort();
    void buildUnclaimed(const Word loadSegment);
    void loadFromMapFile(const std::string &path, const Word reloc);
    void loadFromIdaFile(const std::string &path, const Word reloc);
    std::string routineString(const Routine &r, const Word reloc) const;
};

#endif // ROUTINE_H
