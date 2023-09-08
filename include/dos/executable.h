#ifndef EXECUTABLE_H
#define EXECUTABLE_H

#include <vector>

#include "dos/types.h"
#include "dos/address.h"
#include "dos/segment.h"
#include "dos/memory.h"
#include "dos/routine.h"
#include "dos/mz.h"
#include "dos/analysis.h"

struct AnalysisOptions;

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

#endif // EXECUTABLE_H
