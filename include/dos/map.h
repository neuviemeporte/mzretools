#ifndef MAP_H
#define MAP_H

#include <string>
#include <regex>
#include <vector>

#include "dos/types.h"
#include "dos/address.h"
#include "dos/routine.h"

struct Variable {
    std::string name;
    Address addr;
    static std::smatch stringMatch(const std::string &str);
    Variable(const std::string &name, const Address &addr) : name(name), addr(addr) {}
    std::string toString() const { return name + "/" + addr.toString(); }
    bool operator<(const Variable &other) const { return addr < other.addr; }
};

// A map of an executable, records which areas have been claimed by routines, and which have not, serializable to a file
class CodeMap {
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
    RoutineIdx curId, prevId, curBlockId, prevBlockId;
    bool ida;

public:
    CodeMap(const Word loadSegment, const Size mapSize) : loadSegment(loadSegment), mapSize(mapSize), curId(0), prevId(0), curBlockId(0), prevBlockId(0), ida(false) {}
    CodeMap(const ScanQueue &sq, const std::vector<Segment> &segs, const std::vector<Address> &vars, const Word loadSegment, const Size mapSize);
    CodeMap(const std::string &path, const Word loadSegment = 0);
    CodeMap() : CodeMap(0, 0) {}

    Size codeSize() const { return mapSize; }
    Size routineCount() const { return routines.size(); }
    Size variableCount() const { return vars.size(); }

    // TODO: routine.id start at 1, this is zero based, so id != idx, confusing
    Routine getRoutine(const Size idx) const { return routines.at(idx); }
    Routine getRoutine(const Address &addr) const;
    Routine getRoutine(const std::string &name) const;
    Routine& getMutableRoutine(const Size idx) { return routines.at(idx); }
    Routine& getMutableRoutine(const std::string &name);
    std::vector<Block> getUnclaimed() const { return unclaimed; }
    Variable getVariable(const Size idx) const { return vars.at(idx); }
    Variable getVariable(const std::string &name) const;
    Routine findByEntrypoint(const Address &ep) const;
    Block findCollision(const Block &b) const;
    bool empty() const { return routines.empty(); }
    Size match(const CodeMap &other, const bool onlyEntry) const;
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
    
private:
    void storeDataRef(const Address &dr);
    void closeBlock(Block &b, const Address &next, const ScanQueue &sq, const bool unclaimedOnly);
    Block moveBlock(const Block &b, const Word segment) const;
    void sort();
    void loadFromMapFile(const std::string &path, const Word reloc);
    void loadFromIdaFile(const std::string &path, const Word reloc);
    std::string routineString(const Routine &r, const Word reloc) const;
    std::string varString(const Variable &v, const Word reloc) const;
    void blocksFromQueue(const ScanQueue &sq, const bool unclaimedOnly);
};

#endif // MAP_H