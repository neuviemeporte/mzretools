#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <string>
#include <vector>
#include <list>

#include "dos/types.h"
#include "dos/address.h"
#include "dos/registers.h"
#include "dos/mz.h"

static constexpr int NULL_ROUTINE = 0;

// TODO: 
// - implement calculation of memory offsets based on register values from instruction operand type enum, allow for unknown values, see jump @ 0xab3

class RegisterState {
private:
    static constexpr Word WORD_KNOWN = 0xffff;
    static constexpr Word BYTE_KNOWN = 0xff;
    Registers regs_;
    Registers known_;

public:
    RegisterState();
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

struct SearchPoint {
    Address address;
    int routineId;
    bool isCall;
    RegisterState regs;

    SearchPoint() : routineId(NULL_ROUTINE), isCall(false) {}
    SearchPoint(const Address address, const int id, const bool call, const RegisterState &regs) : address(address), routineId(id), isCall(call), regs(regs) {}
    bool match(const SearchPoint &other) const { return address == other.address && isCall == other.isCall; }
    bool isNull() const { return address.isNull(); }
    std::string toString() const;
};

// utility class for keeping track of the queue of potentially interesting locations (jump/call targets), and which instruction bytes have been claimed by a routine
class SearchQueue {
    friend class SystemTest;
    // memory map for marking which locations belong to which routines, value of 0 is undiscovered
    std::vector<int> visited; // TODO: store addresses from loaded exe in map, otherwise they don't match after analysis done if exe loaded at segment other than 0 
    std::list<SearchPoint> queue;
    std::vector<Address> entrypoints;
    SearchPoint curSearch;
    Address start;

public:
    SearchQueue(const Size codeSize, const SearchPoint &seed);
    // search point queue operations
    Size size() const { return queue.size(); }
    bool empty() const { return queue.empty(); }
    Address startAddress() const { return start; }
    SearchPoint nextPoint();
    bool hasPoint(const Address &dest, const bool call) const;
    void saveCall(const Address &dest, const RegisterState &regs);
    void saveJump(const Address &dest, const RegisterState &regs);
    // discovered locations operations
    Size codeSize() const { return visited.size(); }
    Size routineCount() const { return entrypoints.size(); }
    std::string statusString() const;
    int getRoutineId(const Offset off) const;
    void markRoutine(const Offset off, const Size length, int id = NULL_ROUTINE);
    bool isEntrypoint(const Address &addr) const;
    void dumpVisited(const std::string &path) const;

private:
    SearchQueue() {}
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
    bool colides(const Block &block, const bool checkExtents = true) const;
    std::string toString(const bool showChunks = true) const;
    std::vector<Block> sortedBlocks() const;
};

class RoutineMap {
    friend class SystemTest;
    std::vector<Routine> routines;
    std::vector<Block> unclaimed;
    int curId, prevId, curBlockId, prevBlockId;

public:
    RoutineMap(const SearchQueue &sq);
    RoutineMap(const std::string &path);

    Size size() const { return routines.size(); }
    Routine getRoutine(const Size idx) { return routines.at(idx); }
    bool empty() const { return routines.empty(); }
    Size match(const RoutineMap &other) const;
    Routine colidesBlock(const Block &b) const;
    void save(const std::string &path) const;
    void dump() const;

private:
    RoutineMap() {}
    void closeBlock(Block &b, const Offset off, const SearchQueue &sq);
    void sort();
    void loadFromMapFile(const std::string &path);
    void loadFromIdaFile(const std::string &path);
};

struct Executable {
    std::vector<Byte> code;
    Address entrypoint;
    Address stack;
    Word reloc;
    
    explicit Executable(const MzImage &mz);
    RoutineMap findRoutines();
    bool compareCode(const RoutineMap &map, const Executable &other) const;
};

#endif // ANALYSIS_H