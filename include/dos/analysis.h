#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>

#include "dos/types.h"
#include "dos/address.h"
#include "dos/registers.h"
#include "dos/instruction.h"
#include "dos/routine.h"
#include "dos/scanq.h"
#include "dos/codemap.h"
#include "dos/signature.h"

class Executable;

// TODO: 
// - implement calculation of memory offsets based on register values from instruction operand type enum, allow for unknown values, see jump @ 0xab3 in hello.exe


// utility class used in code analysis routines, keeps mappings of equivalent offsets between two executables for code (calls, jumps), data (global vars) and stack (local vars)
// to make sure they are consistent. This is particulary important in the code comparison routines to know if there is a problem with the executable layout other than just the instructions themselves.
class OffsetMap {
    using MapSet = std::vector<SOffset>;
    Size maxData;
    std::map<Address, Address> codeMap;
    std::map<SOffset, MapSet> dataMap;
    std::map<SOffset, SOffset> stackMap;
    std::vector<Segment> segments;

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
    void addSegment(const Segment &seg) { segments.push_back(seg); }

private:
    std::string dataStr(const MapSet &ms) const;
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

class Analyzer {
    friend class AnalysisTest;
public:
    // TODO: introduce true strict (now it's "not loose"), compare by opcode
    struct Options {
        bool strict, ignoreDiff, noCall, variant, checkAsm, noStats;
        Size refSkip, tgtSkip, ctxCount;
        Size routineSizeThresh; // minimum routine size (in instructions) threshold
        Size routineDistanceThresh; // maximum edit distance threshold (as ratio of routine size)
        Address stopAddr;
        std::string mapPath, tgtMapPath;
        Options() : strict(true), ignoreDiff(false), noCall(false), variant(false), checkAsm(false), noStats(false), refSkip(0), tgtSkip(0), ctxCount(10), 
            routineSizeThresh(15), routineDistanceThresh(10) {}
    };
private:
    enum ComparisonResult { 
        CMP_MISMATCH,
        CMP_MATCH,    // full literal or logical match
        CMP_DIFFVAL,  // match with different immediate/offset value, might be allowed depending on options
        CMP_DIFFTGT,  // match with different branch (near jump) target, might be allowed depending on options
        CMP_VARIANT,  // match with a variant of an instruction or a sequence of instructions
    } matchType;

    enum SkipType {
        SKIP_NONE,
        SKIP_REF,
        SKIP_TGT,
    } skipType;

    Options options;
    Address refCsip, tgtCsip;
    Block compareBlock, targetBlock;
    OffsetMap offMap;
    Size comparedSize, routineSumSize, reachableSize, unreachableSize, excludedSize, excludedCount, excludedReachableSize, missedSize, ignoredSize;
    ScanQueue scanQueue, tgtQueue;
    Routine routine;
    std::set<std::string> routineNames, excludedNames, missedNames;
    Size refSkipCount, tgtSkipCount;
    Address refSkipOrigin, tgtSkipOrigin;
    std::set<Variable> vars;

public:
    Analyzer(const Options &options, const Size maxData = 0) : options(options), offMap(maxData), comparedSize(0) {}
    CodeMap exploreCode(Executable &exe);
    bool compareCode(const Executable &ref, Executable &tgt, const CodeMap &refMap);
    bool compareData(const Executable &ref, const Executable &tgt, const CodeMap &refMap, const CodeMap &tgtMap, const std::string &segment);
    bool findDuplicates(const SignatureLibrary signatures, Executable &tgt, CodeMap &tgtMap);
    void findDataRefs(const Executable &exe, const CodeMap &map);
    void seedQueue(const CodeMap &map, Executable &exe);

private:
    bool skipAllowed(const Instruction &refInstr, Instruction tgtInstr);
    bool compareInstructions(const Executable &ref, const Executable &tgt, const Instruction &refInstr, Instruction tgtInstr);
    void advanceComparison(const Instruction &refInstr, Instruction tgtInstr);
    bool checkComparisonStop();
    void checkMissedRoutines(const CodeMap &refMap);
    Address findTargetLocation(const Executable &ref, const Executable &tgt);
    bool comparisonLoop(const Executable &ref, Executable &tgt, const CodeMap &refMap);
    Branch getBranch(const Executable &exe, const Instruction &i, const CpuState &regs) const;
    ComparisonResult variantMatch(const Executable &tgt, const Instruction &refInstr, Instruction tgtInstr);
    ComparisonResult instructionsMatch(const Executable &ref, const Executable &tgt, const Instruction &refInstr, const Instruction &tgtInstr);
    void diffContext(const Executable &ref, const Executable &tgt) const;
    void skipContext(const Executable &ref, const Executable &tgt) const;
    void calculateStats(const CodeMap &routineMap);
    void comparisonSummary(const Executable &ref, const CodeMap &routineMap, const bool showMissed);
    void processDataReference(const Executable &exe, const Instruction i, const CpuState &regs);
    void claimNops(const Instruction &i, const Executable &exe);
};

#endif // ANALYSIS_H