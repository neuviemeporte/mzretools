#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <string>
#include <vector>
#include <list>
#include <map>

#include "dos/types.h"
#include "dos/address.h"
#include "dos/registers.h"
#include "dos/instruction.h"
#include "dos/routine.h"

static constexpr RoutineId NULL_ROUTINE = 0;

// TODO: 
// - implement calculation of memory offsets based on register values from instruction operand type enum, allow for unknown values, see jump @ 0xab3 in hello.exe

// A destination (jump or call location) inside an analyzed executable
struct Destination {
    Address address;
    RoutineId routineId;
    bool isCall;
    RegisterState regs;

    Destination() : routineId(NULL_ROUTINE), isCall(false) {}
    Destination(const Address address, const RoutineId id, const bool call, const RegisterState &regs) : address(address), routineId(id), isCall(call), regs(regs) {}
    bool match(const Destination &other) const { return address == other.address && isCall == other.isCall; }
    bool isNull() const { return address.isNull(); }
    std::string toString() const;
};

// utility class for keeping track of the queue of potentially interesting Destinations, and which bytes in the executable have been visited already
class ScanQueue {
    friend class AnalysisTest;
    // memory map for marking which locations belong to which routines, value of 0 is undiscovered
    std::vector<RoutineId> visited; // TODO: store addresses from loaded exe in map, otherwise they don't match after analysis done if exe loaded at segment other than 0 
    Address start;
    Destination curSearch;
    std::list<Destination> queue;
    std::vector<RoutineEntrypoint> entrypoints;

public:
    ScanQueue(const Destination &seed);
    // search point queue operations
    Size size() const { return queue.size(); }
    bool empty() const { return queue.empty(); }
    Address startAddress() const { return start; }
    Destination nextPoint();
    bool hasPoint(const Address &dest, const bool call) const;
    bool saveCall(const Address &dest, const RegisterState &regs, const bool near);
    bool saveJump(const Address &dest, const RegisterState &regs);
    // discovered locations operations
    Size routineCount() const { return entrypoints.size(); }
    std::string statusString() const;
    RoutineId getRoutineId(Offset off) const;
    void setRoutineId(Offset off, const Size length, RoutineId id = NULL_ROUTINE);
    RoutineId isEntrypoint(const Address &addr) const;
    std::vector<Routine> getRoutines() const;
    void dumpVisited(const std::string &path, const Offset start = 0, Size size = 0) const;

private:
    ScanQueue() {}
};

class OffsetMap {
    using MapSet = std::vector<SOffset>;
    Size maxData;
    std::map<Address, Address> codeMap;
    std::map<SOffset, MapSet> dataMap;
    std::map<SOffset, SOffset> stackMap;

public:
    // the argument is the maximum number of data segments, we allow as many alternate offset mappings 
    // for a particular offset as there are data segments in the executable. If there is no data segment,
    // then probably this is tiny model (DS=CS) and there is still at least one data segment.
    explicit OffsetMap(const Size maxData) : maxData(maxData > 0 ? maxData : 1) {}
    OffsetMap() : maxData(0) {}
    Address getCode(const Address &from) { return codeMap.count(from) ? codeMap[from] : Address{}; }
    void setCode(const Address &from, const Address &to) { codeMap[from] = to; }
    bool codeMatch(const Address from, const Address to);
    bool dataMatch(const SOffset from, const SOffset to);
    bool stackMatch(const SOffset from, const SOffset to);
    void resetStack() { stackMap.clear(); }

private:
    std::string dataStr(const MapSet &ms) const;
};

struct Branch {
    Address source, destination;
    bool isCall, isUnconditional, isNear;
};

// TODO: introduce true strict (now it's "not loose"), compare by opcode
struct AnalysisOptions {
    bool strict, ignoreDiff, noCall, variant, checkAsm;
    Size refSkip, tgtSkip, ctxCount;
    Address stopAddr;
    AnalysisOptions() : strict(true), ignoreDiff(false), noCall(false), variant(false), refSkip(0), tgtSkip(0), ctxCount(10) {}
};

// TODO: compare instructions, not string representations, allow wildcards in place of arguments, e.g. "mov ax, *"
class VariantMap {
private: 
    using Variant = std::vector<std::string>; // a variant is a bunch of strings representing one or more instructions in a sequence
    using Bucket = std::vector<Variant>; // a bucket is a bunch of variants, i.e. instructions or instructions sequences that are equivalent to each other
    std::vector<Bucket> buckets_;
    Size maxDepth_; // maximum depth, i.e. length of instruction sequence found in any bucket (worst case scenario to compare)

public:
    // a helper type returned as a comparison result
    struct MatchDepth {
        int left, right; // amount of instructions matched on the left and right as variants
        MatchDepth() : left(0), right(0) {}
        MatchDepth(const int left, const int right) : left(left), right(right) {}
        bool isMatch() const { return left != 0 && right != 0; }
    };
    VariantMap();
    explicit VariantMap(std::istream &str);
    explicit VariantMap(const std::string &path);
    Size maxDepth() const { return maxDepth_; }
    MatchDepth checkMatch(const Variant &left, const Variant &right);
    // for debugging
    void dump() const;

private:
    int find(const Variant &search, int bucket) const;
    void loadFromStream(std::istream &str);
};

#endif // ANALYSIS_H