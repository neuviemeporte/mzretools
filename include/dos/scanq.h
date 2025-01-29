#ifndef SCANQ_H
#define SCANQ_H

#include <list>

#include "dos/address.h"
#include "dos/routine.h"
#include "dos/registers.h"

// A destination (jump or call location) inside an analyzed executable
struct Destination {
    Address address;
    RoutineIdx routineIdx;
    bool isCall;
    RegisterState regs;

    Destination() : routineIdx(NULL_ROUTINE), isCall(false) {}
    Destination(const Address address, const RoutineIdx idx, const bool call, const RegisterState &regs) : address(address), routineIdx(idx), isCall(call), regs(regs) {}
    bool match(const Destination &other) const { return address == other.address && isCall == other.isCall; }
    bool isNull() const { return address.isNull(); }
    std::string toString() const;
};

struct Branch {
    Address source, destination;
    bool isCall, isConditional, isNear;
    Branch() : isCall(false), isConditional(false), isNear(true) {}
    std::string toString() const;
};

// utility class for keeping track of the queue of potentially interesting Destinations, and which bytes in the executable have been visited already
class ScanQueue {
    friend class AnalysisTest;
    // memory map for marking which locations belong to which routines, value of 0 is undiscovered
    // TODO: store addresses from loaded exe in map, otherwise they don't match after analysis done if exe loaded at segment other than 0
    std::vector<RoutineIdx> visited;
    Address origin;
    Destination curSearch;
    std::list<Destination> queue;
    std::vector<RoutineEntrypoint> entrypoints;

public:
    ScanQueue(const Address &origin, const Size codeSize, const Destination &seed, const std::string name = {});
    ScanQueue() : origin(0, 0) {}
    // search point queue operations
    Size size() const { return queue.size(); }
    bool empty() const { return queue.empty(); }
    Address originAddress() const { return origin; }
    Destination nextPoint();
    bool hasPoint(const Address &dest, const bool call) const;
    bool saveCall(const Address &dest, const RegisterState &regs, const bool near, const std::string name = {});
    bool saveJump(const Address &dest, const RegisterState &regs);
    bool saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents);
    // discovered locations operations
    Size routineCount() const { return entrypoints.size(); }
    std::string statusString() const;
    RoutineIdx getRoutineIdx(Offset off) const;
    void setRoutineIdx(Offset off, const Size length, RoutineIdx idx = NULL_ROUTINE);
    void clearRoutineIdx(Offset off);
    RoutineIdx isEntrypoint(const Address &addr) const;
    RoutineEntrypoint getEntrypoint(const std::string &name);
    std::vector<Routine> getRoutines() const;
    std::vector<Block> getUnvisited() const;
    void dumpVisited(const std::string &path) const;
    void dumpEntrypoints() const;
};

#endif // SCANQ_H