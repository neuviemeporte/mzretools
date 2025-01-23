#ifndef ROUTINE_H
#define ROUTINE_H

#include <string>
#include <vector>

#include "dos/types.h"
#include "dos/address.h"

using RoutineId = int;
static constexpr RoutineId 
    BAD_ROUTINE = -1,
    NULL_ROUTINE = 0;

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
    std::vector<std::string> comments;
    RoutineId id;
    bool near, ignore, complete, unclaimed, external, detached, assembly, duplicate;

    Routine(const std::string &name, const Block &extents) : name(name), extents(extents), id(NULL_ROUTINE),
        near(true), ignore(false), complete(false), unclaimed(false), external(false), detached(false), assembly(false), duplicate(false) {}
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
    std::string dump(const bool showChunks = true) const;
    std::string toString() const;
    std::vector<Block> sortedBlocks() const;
    void recalculateExtents();
    void addComment(const std::string &comment) { comments.push_back(comment); }
};

struct Variable {
    std::string name;
    Address addr;
    static std::smatch stringMatch(const std::string &str);
    Variable(const std::string &name, const Address &addr) : name(name), addr(addr) {}
};

// A map of an executable, records which areas have been claimed by routines, and which have not, serializable to a file
// TODO: name no longer fits since it also keeps track of data
class RoutineMap {
public:
    struct Summary {
        std::string text;
        Size codeSize, ignoredSize, completedSize, unclaimedSize, externalSize, dataCodeSize, detachedSize, assemblySize;
        Size ignoreCount, completeCount, unclaimedCount, externalCount, dataCodeCount, detachedCount, assemblyCount;
        Size dataSize, otherSize, otherCount, ignoredReachableSize, ignoredReachableCount, uncompleteSize, uncompleteCount, unaccountedSize, unaccountedCount;
        Summary() {
            codeSize = ignoredSize = completedSize = unclaimedSize = externalSize = dataCodeSize = detachedSize = assemblySize = 0;
            ignoreCount = completeCount = unclaimedCount = externalCount = dataCodeCount = detachedCount = assemblyCount = 0;
            dataSize = otherSize = otherCount = ignoredReachableSize = ignoredReachableCount = uncompleteSize = uncompleteCount = unaccountedSize = unaccountedCount = 0;
        }
    };
private:
    friend class AnalysisTest;
    Word loadSegment;
    Size mapSize;
    std::vector<Routine> routines;
    std::vector<Block> unclaimed;
    std::vector<Segment> segments;
    std::vector<Variable> vars;
    // TODO: turn these into a context struct, pass around instead of members
    RoutineId curId, prevId, curBlockId, prevBlockId;
    bool ida;

public:
    RoutineMap(const Word loadSegment, const Size mapSize) : loadSegment(loadSegment), mapSize(mapSize), curId(0), prevId(0), curBlockId(0), prevBlockId(0), ida(false) {}
    RoutineMap(const ScanQueue &sq, const std::vector<Segment> &segs, const Word loadSegment, const Size mapSize);
    RoutineMap(const std::string &path, const Word loadSegment = 0);
    RoutineMap() : RoutineMap(0, 0) {}

    Size routineCount() const { return routines.size(); }
    // TODO: routine.id start at 1, this is zero based, so id != idx, confusing
    Routine getRoutine(const Size idx) const { return routines.at(idx); }
    Routine getRoutine(const Address &addr) const;
    Routine getRoutine(const std::string &name) const;
    Routine& getMutableRoutine(const Size idx) { return routines.at(idx); }
    Routine& getMutableRoutine(const std::string &name);
    std::vector<Block> getUnclaimed() const { return unclaimed; }
    Routine findByEntrypoint(const Address &ep) const;
    Block findCollision(const Block &b) const;
    bool empty() const { return routines.empty(); }
    Size match(const RoutineMap &other, const bool onlyEntry) const;
    bool isIda() const { return ida; }
    Routine colidesBlock(const Block &b) const;
    void order();
    void save(const std::string &path, const Word reloc = 0, const bool overwrite = false) const;
    Summary getSummary(const bool verbose = true, const bool hide = false, const bool format = false) const;
    const auto& getSegments() const { return segments; }
    Size segmentCount(const Segment::Type type) const;
    Segment findSegment(const Word addr) const;
    Segment findSegment(const std::string &name) const;
    Segment findSegment(const Offset off) const;
    void setSegments(const std::vector<Segment> &seg);
    void buildUnclaimed();
    void storeDataRef(const Address &dr);
    
private:
    void closeBlock(Block &b, const Address &next, const ScanQueue &sq);
    void closeUnclaimedBlock(const Block &ub);
    Block moveBlock(const Block &b, const Word segment) const;
    void sort();
    void loadFromMapFile(const std::string &path, const Word reloc);
    void loadFromIdaFile(const std::string &path, const Word reloc);
    std::string routineString(const Routine &r, const Word reloc) const;
    std::string varString(const Variable &v, const Word reloc) const;
};

#endif // ROUTINE_H
