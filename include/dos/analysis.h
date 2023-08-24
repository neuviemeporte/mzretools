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
#include "dos/mz.h"
#include "dos/memory.h"

using RoutineId = int;
static constexpr RoutineId NULL_ROUTINE = 0;

// TODO: 
// - implement calculation of memory offsets based on register values from instruction operand type enum, allow for unknown values, see jump @ 0xab3 in hello.exe

class RegisterState {
private:
    static constexpr Word WORD_KNOWN = 0xffff;
    static constexpr Word BYTE_KNOWN = 0xff;
    Registers regs_;
    Registers known_;

public:
    RegisterState();
    RegisterState(const Address &code, const Address &stack);
    bool isKnown(const Register r) const;
    Word getValue(const Register r) const;
    void setValue(const Register r, const Word value);
    void setUnknown(const Register r);
    std::string regString(const Register r) const;
    std::string toString() const;

private:
    void setState(const Register r, const Word value, const bool known);
    std::string stateString(const Register r) const;
};

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

    Routine() {}
    Routine(const std::string &name, const Block &extents) : name(name), extents(extents) {}
    Address entrypoint() const { return extents.begin; }
    // for sorting purposes
    bool operator<(const Routine &other) { return entrypoint() < other.entrypoint(); }
    bool isValid() const { return extents.isValid(); }
    bool isUnchunked() const { return reachable.size() == 1 && unreachable.empty() && reachable.front() == extents; }
    bool isReachable(const Block &b) const;
    bool isUnreachable(const Block &b) const;
    Block mainBlock() const;
    Block blockContaining(const Address &a) const;
    bool colides(const Block &block, const bool checkExtents = true) const;
    std::string toString(const bool showChunks = true) const;
    std::vector<Block> sortedBlocks() const;
};

// utility class for keeping track of the queue of potentially interesting Destinations, and which bytes in the executable have been visited already
class ScanQueue {
    friend class SystemTest;
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
    Destination nextPoint(const bool front = true);
    bool hasPoint(const Address &dest, const bool call) const;
    bool saveCall(const Address &dest, const RegisterState &regs);
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

struct Segment {
    enum Type {
        SEG_NONE,
        SEG_CODE,
        SEG_DATA,
        SEG_STACK
    } type;
    Word value;
    Segment(Type type, Word value) : type(type), value(value) {}
    Segment(const std::string &str);
    bool operator==(const Segment &other) const { return type == other.type && value == other.value; }
    std::string toString() const;
};

// A map of an executable, records which areas have been claimed by routines, and which have not, serializable to a file
class RoutineMap {
    friend class SystemTest;
    Word reloc;
    Size codeSize;
    std::vector<Routine> routines;
    std::vector<Block> unclaimed;
    std::vector<Segment> segments;
    // TODO: turn these into a context struct, pass around instead of members
    RoutineId curId, prevId, curBlockId, prevBlockId;

public:
    RoutineMap() : reloc(0) {}
    RoutineMap(const ScanQueue &sq, const Word loadSegment, const Size codeSize);
    RoutineMap(const std::string &path, const Word reloc = 0);

    Size size() const { return routines.size(); }
    Routine getRoutine(const Size idx) const { return routines.at(idx); }
    Routine getRoutine(const Address &addr) const;
    bool empty() const { return routines.empty(); }
    Size match(const RoutineMap &other) const;
    Routine colidesBlock(const Block &b) const;
    void save(const std::string &path, const bool overwrite = false) const;
    std::string dump() const;
    void setSegments(const std::vector<Segment> &seg);
    const auto& getSegments() const { return segments; }
    Size segmentCount(const Segment::Type type) const;
    
private:
    void closeBlock(Block &b, const Offset off, const ScanQueue &sq);
    Block moveBlock(const Block &b, const Word segment) const;
    void sort();
    void loadFromMapFile(const std::string &path);
    void loadFromIdaFile(const std::string &path);
};

class OffsetMap {
    using MapSet = std::vector<SOffset>;
    Size maxData;
    std::map<Address, Address> codeMap;
    std::map<SOffset, MapSet> dataMap;
    std::map<SOffset, SOffset> stackMap;

public:
    OffsetMap() : maxData(0) {}
    // the argument is the maximum number of data segments, we allow as many alternate offset mappings 
    // for a particular offset as there are data segments in the executable. If there is no data segment,
    // then probably this is tiny model (DS=CS) and there is still at least one data segment.
    explicit OffsetMap(const Size maxData) : maxData(maxData > 0 ? maxData : 1) {}
    Address getCode(const Address &from) { return codeMap[from]; }
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
    bool isCall, isUnconditional;
};

// TODO: introduce true strict (now it's "not loose"), compare by opcode
struct AnalysisOptions {
    bool strict, ignoreDiff, noCall, variant;
    Size refSkip, objSkip;
    Address stopAddr;
    AnalysisOptions() : strict(true), ignoreDiff(false), noCall(false), variant(false), refSkip(0), objSkip(0) {}
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

// TODO: make testable, constructor from instruction data
class Executable {
    friend class SystemTest;
    const Memory code;
    Word loadSegment;
    Size codeSize;
    Address ep, stack;
    Block codeExtents;
    std::vector<Segment> segments;

    enum ComparisonResult { 
        CMP_MISMATCH,
        CMP_MATCH,    // full literal or logical match
        CMP_DIFFVAL,  // match with different literal value, might be allowed depending on options
        CMP_VARIANT,  // match with a variant of an instruction or a sequence of instructions
    };

    enum SkipType {
        SKIP_NONE,
        SKIP_REF,
        SKIP_OBJ,
    };

public:
    explicit Executable(const MzImage &mz);
    Executable(const Word loadSegment, const std::vector<Byte> &data);
    const Address& entrypoint() const { return ep; }
    void setEntrypoint(const Address &addr);

    bool contains(const Address &addr) const { return codeExtents.contains(addr); }
    RoutineMap findRoutines();
    bool compareCode(const RoutineMap &map, const Executable &other, const AnalysisOptions &options);

private:
    struct Context {
        const Executable &other;
        const AnalysisOptions &options;
        Address csip, otherCsip;
        OffsetMap offMap;
        Context(const Executable &other, const AnalysisOptions &opt, const Size maxData);
    };
    void init();
    void searchMessage(const Address &addr, const std::string &msg) const;
    Branch getBranch(const Instruction &i, const RegisterState &regs = {}) const;
    bool saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents, ScanQueue &sq) const;
    void applyMov(const Instruction &i, RegisterState &regs);

    ComparisonResult instructionsMatch(Context &ctx, const Instruction &ref, Instruction obj);
    void storeSegment(const Segment &seg);
    void diffContext(const Context &ctx) const;
    void skipContext(const Context &ctx, Address refAddr, Address objAddr, Size refSkipped, Size objSkipped) const;
};

#endif // ANALYSIS_H