#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <string>
#include <vector>
#include <list>

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
    void saveCall(const Address &dest, const RegisterState &regs);
    void saveJump(const Address &dest, const RegisterState &regs);
    // discovered locations operations
    Size routineCount() const { return entrypoints.size(); }
    std::string statusString() const;
    RoutineId getRoutineId(Offset off) const;
    void setRoutineId(Offset off, const Size length, RoutineId id = NULL_ROUTINE);
    RoutineId isEntrypoint(const Address &addr) const;
    void dumpVisited(const std::string &path, const Offset start = 0, Size size = 0) const;

private:
    ScanQueue() {}
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

class RoutineMap {
    friend class SystemTest;
    Word reloc;
    Size codeSize;
    std::vector<Routine> routines;
    std::vector<Block> unclaimed;
    // TODO: turn these into a context struct, pass around instead of members
    RoutineId curId, prevId, curBlockId, prevBlockId;

public:
    RoutineMap(const ScanQueue &sq, const Word loadSegment, const Size codeSize);
    RoutineMap(const std::string &path, const Word reloc = 0);

    Size size() const { return routines.size(); }
    Routine getRoutine(const Size idx) const { return routines.at(idx); }
    Routine getRoutine(const Address &addr) const;
    bool empty() const { return routines.empty(); }
    Size match(const RoutineMap &other) const;
    Routine colidesBlock(const Block &b) const;
    void save(const std::string &path) const;
    std::string dump() const;

private:
    RoutineMap() : reloc(0) {}
    void closeBlock(Block &b, const Offset off, const ScanQueue &sq);
    void sort();
    void loadFromMapFile(const std::string &path);
    void loadFromIdaFile(const std::string &path);
};

struct Branch {
    Address destination;
    bool isCall, isUnconditional;
};

class Executable {
    friend class SystemTest;
    const Memory code;
    Word loadSegment;
    Size codeSize;
    Address entrypoint;
    Address csip, stack;
    Block codeExtents;

public:
    explicit Executable(const MzImage &mz, const Address &entrypoint = Address());
    bool contains(const Address &addr) const { return codeExtents.contains(addr); }
    RoutineMap findRoutines();
    bool compareCode(const RoutineMap &map, const Executable &other);

private:
    void searchMessage(const std::string &msg) const;
    Branch getBranch(const Instruction &i, const RegisterState &regs) const;
    void saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents, ScanQueue &sq) const;
    void applyMov(const Instruction &i, RegisterState &regs) const;

    void setEntrypoint(const Address &addr);
};

#endif // ANALYSIS_H