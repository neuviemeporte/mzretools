#ifndef EXECUTABLE_H
#define EXECUTABLE_H

#include <vector>
#include <set>

#include "dos/types.h"
#include "dos/address.h"
#include "dos/memory.h"
#include "dos/routine.h"
#include "dos/mz.h"
#include "dos/analysis.h"

struct AnalysisOptions;

class Executable {
    friend class AnalysisTest;
    const Memory code;
    Word loadSegment;
    Size codeSize;
    Address ep, stack;
    Block codeExtents;
    std::vector<Segment> segments;

    enum ComparisonResult { 
        CMP_MISMATCH,
        CMP_MATCH,    // full literal or logical match
        CMP_DIFFVAL,  // match with different immediate/offset value, might be allowed depending on options
        CMP_DIFFTGT,  // match with different branch (near jump) target, might be allowed depending on options
        CMP_VARIANT,  // match with a variant of an instruction or a sequence of instructions
    };

    enum SkipType {
        SKIP_NONE,
        SKIP_REF,
        SKIP_TGT,
    };

public:
    explicit Executable(const MzImage &mz);
    Executable(const Word loadSegment, const std::vector<Byte> &data);
    const Address& entrypoint() const { return ep; }
    void setEntrypoint(const Address &addr);

    Size size() const { return codeSize; }
    bool contains(const Address &addr) const { return codeExtents.contains(addr); }
    RoutineMap findRoutines();
    bool compareCode(const RoutineMap &map, const Executable &other, const AnalysisOptions &options);

private:
    struct Context {
        const Executable &target;
        const AnalysisOptions &options;
        Address refCsip, tgtCsip;
        OffsetMap offMap;
        Context(const Executable &target, const AnalysisOptions &opt, const Size maxData);
    };
    void init();
    void searchMessage(const Address &addr, const std::string &msg) const;
    Branch getBranch(const Instruction &i, const RegisterState &regs = {}) const;
    bool saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents, ScanQueue &sq) const;
    void applyMov(const Instruction &i, RegisterState &regs);

    ComparisonResult instructionsMatch(Context &ctx, const Instruction &ref, Instruction tgt);
    void storeSegment(const Segment::Type type, const Word addr);
    void diffContext(const Context &ctx) const;
    void skipContext(const Context &ctx, Address refAddr, Address tgtAddr, Size refSkipped, Size tgtSkipped) const;
    void compareSummary(const Size comparedSize, const RoutineMap &routineMap, 
        const std::set<std::string> &routineNames, const std::set<std::string> &excludedNames, const AnalysisOptions &options, const bool showMissed) const;
};

#endif // EXECUTABLE_H
