#ifndef ROUTINE_H
#define ROUTINE_H

#include <string>
#include <vector>

#include "dos/types.h"
#include "dos/address.h"

using RoutineIdx = int;
static constexpr RoutineIdx 
    BAD_ROUTINE = -1,
    NULL_ROUTINE = 0,
    VISITED_ID = 1;

class ScanQueue;

struct RoutineEntrypoint {
    Address addr;
    RoutineIdx idx;
    bool near;
    std::string name;

    RoutineEntrypoint(const Address &addr, const RoutineIdx idx, const bool near = true) : addr(addr), idx(idx), near(near) {}
    RoutineEntrypoint() : RoutineEntrypoint({}, NULL_ROUTINE, false) {}
    bool operator==(const Address &arg) const { return addr == arg; }
    std::string toString() const;
};

struct Routine {
    std::string name;
    Block extents; // largest contiguous block starting at routine entrypoint, may contain unreachable regions
    std::vector<Block> reachable, unreachable;
    std::vector<std::string> comments;
    RoutineIdx idx;
    bool near, ignore, complete, unclaimed, external, detached, assembly, duplicate;

    Routine(const std::string &name, const Block &extents) : name(name), extents(extents), idx(NULL_ROUTINE),
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

#endif // ROUTINE_H
