#include <map>
#include <vector>
#include <algorithm>
#include <regex>

#include "dos/executable.h"
#include "dos/analysis.h"
#include "dos/output.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

// dictionary of equivalent instruction sequences for variant-enabled comparison
// TODO: support user-supplied equivalence dictionary 
// TODO: do not hardcode, place in text file
// TODO: automatic reverse match generation
// TODO: replace strings with Instruction-s, support more flexible matching?
static const map<string, vector<vector<string>>> INSTR_VARIANT = {
    { "add sp, 0x2", { 
            { "pop cx" }, 
            { "inc sp", "inc sp" },
        },
    },
    { "add sp, 0x4", { 
            { "pop cx", "pop cx" }, 
        },
    },
    { "sub ax, ax", { 
            { "xor ax, ax" }, 
        },
    },    
};


// TODO: include information of existing address mapping contributing to a match/mismatch in the output
// TODO: make jump instructions compare the match with the relative offset value
static string compareStatus(const Instruction &i1, const Instruction &i2, const bool align, InstructionMatch match = INS_MATCH_ERROR) {
    static const int ALIGN = 50;
    string status;
    if (i1.isValid()) {
        status = i1.addr.toString() + ": " + i1.toString(align);
    }
    if (!i2.isValid()) return status;

    if (align && status.length() < ALIGN) status.append(ALIGN - status.length(), ' ');
    if (i1.isValid()) {
        if (match == INS_MATCH_ERROR) match = i1.match(i2);
        switch (match) {
        case INS_MATCH_FULL:     status += " == "; break;
        case INS_MATCH_DIFF:     status += " ~~ "; break;
        case INS_MATCH_DIFFOP1:  status += " ~= "; break;
        case INS_MATCH_DIFFOP2:  status += " =~ "; break;
        case INS_MATCH_MISMATCH: status += " != "; break; 
        }
    }
    else status.append(4, ' ');
    status += i2.addr.toString() + ": " + i2.toString(align);
    return status;
}

Executable::Executable(const MzImage &mz) : 
    code(mz.loadSegment(), mz.loadModuleData(), mz.loadModuleSize()),
    loadSegment(mz.loadSegment()),
    codeSize(mz.loadModuleSize()),
    stack(mz.stackPointer())
{
    // relocate entrypoint
    setEntrypoint(mz.entrypoint());
    init();
}

Executable::Executable(const Word loadSegment, const std::vector<Byte> &data) :
    code(loadSegment, data.data(), data.size()),
    loadSegment(loadSegment),
    codeSize(data.size()),
    stack{}
{
    setEntrypoint({0, 0});
    init();
}

// common initialization after construction
void Executable::init() {
    codeExtents = Block{{loadSegment, Word(0)}, Address(SEG_TO_OFFSET(loadSegment) + codeSize - 1)};
    stack.relocate(loadSegment);
    debug("Loaded executable data into memory, code at "s + codeExtents.toString() + ", relocated entrypoint " + entrypoint().toString() + ", stack " + stack.toString());    
}

void Executable::setEntrypoint(const Address &addr) {
    ep = addr;
    ep.relocate(loadSegment);
}

Executable::Context::Context(const Executable &target, const AnalysisOptions &opt, const Size maxData) 
        : target(target), options(opt), offMap(maxData)
{
}

void Executable::searchMessage(const Address &addr, const string &msg) const {
    output(addr.toString() + ": " + msg, LOG_ANALYSIS, LOG_DEBUG);
}

// TODO: this should be in the CPU class, RegisterState as injectable drop-in replacement for regular Registers for CPU
Branch Executable::getBranch(const Instruction &i, const RegisterState &regs) const {
    Branch branch;
    branch.isCall = false;
    branch.isUnconditional = false;
    branch.isNear = true;
    const Address &addr = i.addr;
    switch (i.iclass) {
    // conditional jump
    case INS_JMP_IF:
        branch.destination = i.relativeAddress();
        searchMessage(addr, "encountered conditional near jump to "s + branch.destination.toString());
        break;
    // unconditional jumps
    case INS_JMP:
        switch (i.opcode) {
        case OP_JMP_Jb: 
        case OP_JMP_Jv: 
            branch.destination = i.relativeAddress();
            branch.isUnconditional = true;
            searchMessage(addr, "encountered unconditional near jump to "s + branch.destination.toString());
            break;
        case OP_GRP5_Ev:
            branch.isUnconditional = true;
            searchMessage(addr, "unknown near jump target: "s + i.toString());
            debug(regs.toString());
            break; 
        default: 
            throw AnalysisError("Unsupported jump opcode: "s + hexVal(i.opcode));
        }
        break;
    case INS_JMP_FAR:
        if (i.op1.type == OPR_IMM32) {
            branch.destination = Address{i.op1.immval.u32}; 
            branch.isUnconditional = true;
            branch.isNear = false;
            searchMessage(addr, "encountered unconditional far jump to "s + branch.destination.toString());
        }
        else {
            searchMessage(addr, "unknown far jump target: "s + i.toString());
            debug(regs.toString());
        }
        break;
    // loops
    case INS_LOOP:
    case INS_LOOPNZ:
    case INS_LOOPZ:
        branch.destination = i.relativeAddress();
        searchMessage(addr, "encountered loop to "s + branch.destination.toString());
        break;
    // calls
    case INS_CALL:
        if (i.op1.type == OPR_IMM16) {
            branch.destination = i.relativeAddress();
            branch.isCall = true;
            searchMessage(addr, "encountered near call to "s + branch.destination.toString());
        }
        else if (operandIsReg(i.op1.type) && regs.isKnown(i.op1.regId())) {
            branch.destination = {i.addr.segment, regs.getValue(i.op1.regId())};
            branch.isCall = true;
            searchMessage(addr, "encountered near call through register to "s + branch.destination.toString());
        }
        else if (operandIsMemImmediate(i.op1.type) && regs.isKnown(REG_DS)) {
            // need to read call destination offset from memory
            Address memAddr{regs.getValue(REG_DS), i.op1.immval.u16};
            if (codeExtents.contains(memAddr)) {
                branch.destination = Address{i.addr.segment, code.readWord(memAddr)};
                branch.isCall = true;
                searchMessage(addr, "encountered near call through mem pointer to "s + branch.destination.toString());
            }
            else debug("mem pointer of call destination outside code extents: " + memAddr.toString());
        }
        else {
            searchMessage(addr, "unknown near call target: "s + i.toString());
            debug(regs.toString());
        } 
        break;
    case INS_CALL_FAR:
        if (i.op1.type == OPR_IMM32) {
            branch.destination = Address(DWORD_SEGMENT(i.op1.immval.u32), DWORD_OFFSET(i.op1.immval.u32));
            branch.isCall = true;
            branch.isNear = false;
            searchMessage(addr, "encountered far call to "s + branch.destination.toString());
        }
        else {
            searchMessage(addr, "unknown far call target: "s + i.toString());
            debug(regs.toString());
        }
        break;
    default:
        throw AnalysisError("Instruction is not a branch: "s + i.toString() + ", class = " + instr_class_name(i.iclass));
    }
    return branch;
}

void Executable::applyMov(const Instruction &i, RegisterState &regs) {
    // we only care about mov-s into registers
    if (i.iclass != INS_MOV || !operandIsReg(i.op1.type)) return;

    const Register dest = i.op1.regId();
    bool set = false;
    // mov reg, imm
    if (operandIsImmediate(i.op2.type)) {
        switch(i.op2.size) {
        case OPRSZ_BYTE: 
            regs.setValue(dest, i.op2.immval.u8); 
            set = true; 
            break;
        case OPRSZ_WORD: 
            regs.setValue(dest, i.op2.immval.u16);
            set = true; 
            break;
        }
    }
    // mov reg, reg
    else if (operandIsReg(i.op2.type) && regs.isKnown(i.op2.regId())) {
        const Register src = i.op2.regId();
        regs.setValue(dest, regs.getValue(src));
        set = true;
    }
    // mov reg, mem
    else if (operandIsMemImmediate(i.op2.type)) {
        const Register segReg = prefixRegId(i.prefix);
        // ignore data located in the stack segment, it's not reliable even if within code extents
        if (segReg == REG_SS || !regs.isKnown(segReg)) return;
        Address srcAddr(regs.getValue(segReg), i.op2.immval.u16);
        if (codeExtents.contains(srcAddr)) {
            switch(i.op2.size) {
            case OPRSZ_BYTE:
                searchMessage(i.addr, "source address for byte: " + srcAddr.toString());
                regs.setValue(dest, code.readByte(srcAddr)); 
                set = true;
                break;
            case OPRSZ_WORD:
                searchMessage(i.addr, "source address for word: " + srcAddr.toString());
                regs.setValue(dest, code.readWord(srcAddr));
                set = true;
                break;
            }
        }
        else searchMessage(i.addr, "mov source address outside code extents: "s + srcAddr.toString());
    }
    if (set) {
        searchMessage(i.addr, "executed move to register: "s + i.toString());
        if (i.op1.type == OPR_REG_DS) storeSegment(Segment::SEG_DATA, regs.getValue(REG_DS));
        else if (i.op1.type == OPR_REG_SS) storeSegment(Segment::SEG_STACK, regs.getValue(REG_SS));
    }
}

Executable::ComparisonResult Executable::instructionsMatch(Context &ctx, const Instruction &ref, Instruction tgt) {
    if (ctx.options.ignoreDiff) return CMP_MATCH;

    auto insResult = ref.match(tgt);
    if (insResult == INS_MATCH_FULL) return CMP_MATCH;

    bool match = false;
    // instructions differ in value of immediate or memory offset
    if (insResult == INS_MATCH_DIFFOP1 || insResult == INS_MATCH_DIFFOP2) {
        const auto &refop = (insResult == INS_MATCH_DIFFOP1 ? ref.op1 : ref.op2);
        const auto &tgtop = (insResult == INS_MATCH_DIFFOP1 ? tgt.op1 : tgt.op2);
        if (ref.isBranch()) {
            const Branch 
                refBranch = getBranch(ref), 
                tgtObranch = getBranch(tgt);
            if (refBranch.destination.isValid() && tgtObranch.destination.isValid()) {
                match = ctx.offMap.codeMatch(refBranch.destination, tgtObranch.destination);
                if (!match) debug("Instruction mismatch on branch destination");
            }
            // special case of jmp vs jmp short - allow only if variants enabled
            if (match && ref.opcode != tgt.opcode && (ref.isUnconditionalJump() || tgt.isUnconditionalJump())) {
                if (ctx.options.variant) {
                    verbose(output_color(OUT_YELLOW) + compareStatus(ref, tgt, true, INS_MATCH_DIFF) + output_color(OUT_DEFAULT));
                    ctx.tgtCsip += tgt.length;
                    return CMP_VARIANT;
                }
                else return CMP_MISMATCH;
            }
        }
        else if (operandIsMemWithOffset(refop.type)) {
            // in strict mode, the offsets are expected to be exactly matching, with no translation
            if (ctx.options.strict) {
                debug("Mismatching due to offset difference in strict mode");
                return CMP_MISMATCH;
            }
            // otherwise, apply mapping
            SOffset refOfs = ref.memOffset(), tgtOfs = tgt.memOffset();
            Register segReg = ref.memSegmentId();
            switch (segReg) {
            case REG_CS:
                match = ctx.offMap.codeMatch(refOfs, tgtOfs);
                if (!match) verbose("Instruction mismatch due to code segment offset mapping conflict");
                break;
            case REG_ES: /* TODO: come up with something better */
            case REG_DS:
                match = ctx.offMap.dataMatch(refOfs, tgtOfs);
                if (!match) verbose("Instruction mismatch due to data segment offset mapping conflict");
                break;
            case REG_SS:
                if (!ctx.options.strict) {
                    match = ctx.offMap.stackMatch(refOfs, tgtOfs);
                    if (!match) verbose("Instruction mismatch due to stack segment offset mapping conflict");
                }
                break;
            default:
                throw AnalysisError("Unsupported segment register in instruction comparison: " + regName(segReg));
                break;
            }
            return match ? CMP_DIFFVAL : CMP_MISMATCH;
        }
        else if (operandIsImmediate(refop.type) && !ctx.options.strict) {
            debug("Ignoring immediate value difference in loose mode");
            return CMP_DIFFVAL;
        }
    }
    
    if (match) return CMP_MATCH;
    // check for a variant match if allowed by options
    else if (ctx.options.variant && INSTR_VARIANT.count(ref.toString())) {
        // get vector of allowed variants (themselves vectors of strings)
        const auto &variants = INSTR_VARIANT.at(ref.toString());
        debug("Found "s + to_string(variants.size()) + " variants for instruction '" + ref.toString() + "'");
        // compose string for showing the variant comparison instructions
        string statusStr = compareStatus(ref, tgt, true, INS_MATCH_DIFF);
        string variantStr;
        // iterate over the possible variants of this reference instruction
        for (auto &v : variants) {
            variantStr.clear();
            match = true;
            // temporary code pointer for the target binary while we scan its instructions ahead
            Address tmpCsip = ctx.tgtCsip;
            int idx = 0;
            // iterate over instructions inside this variant
            for (auto &istr: v) {
                debug(tmpCsip.toString() + ": " + tgt.toString() + " == " + istr + " ? (" + to_string(idx+1) + "/" + to_string(v.size()) + ")");
                // stringwise compare the next instruction in the variant to the current instruction
                if (tgt.toString() != istr) { match = false; break; }
                // if this is not the last instruction in the variant, read the next instruction from the target binary
                tmpCsip += tgt.length;
                if (++idx < v.size()) {
                    tgt = Instruction{tmpCsip, ctx.target.code.pointer(tmpCsip)};
                    variantStr += "\n" + compareStatus(Instruction(), tgt, true);
                }
            }
            if (match) {
                debug("Got variant match, advancing target binary to " + tmpCsip.toString());
                verbose(output_color(OUT_YELLOW) + statusStr + variantStr + output_color(OUT_DEFAULT));
                // in the case of a match, need to update the actual instruction pointer in the target binary to account for the instructions we skipped
                ctx.tgtCsip = tmpCsip;
                return CMP_VARIANT;
            }
        }
    }
    
    return CMP_MISMATCH;
}

void Executable::storeSegment(const Segment::Type type, const Word addr) {
    // ignore segments which are already known
    auto found = std::find_if(segments.begin(), segments.end(), [=](const Segment &s){
        return s.type == type && s.address == addr;
    });
    if (found != segments.end()) return;
    // check if an existing segment of a different type has the same address
    found = std::find_if(segments.begin(), segments.end(), [&](const Segment &s){ 
        return s.address == addr;
    });
    if (found != segments.end()) {
        const Segment &existing = *found;
        // existing code segments cannot share an address with another segment
        if (existing.type == Segment::SEG_CODE) return;
        // a new data segment trumps an existing stack segment
        else if (existing.type == Segment::SEG_STACK && type == Segment::SEG_DATA) {
            segments.erase(found);
        }
        else return;
    }
    // compose name for new segment
    int idx=1;
    for (const auto &s : segments) {
        if (s.type == type) idx++;
    }
    string segName;
    switch (type) {
    case Segment::SEG_CODE: segName = "Code"; break;
    case Segment::SEG_DATA: segName = "Data"; break;
    case Segment::SEG_STACK: segName = "Stack"; break;
    default: throw AnalysisError("Unsupported segment type");
    }
    segName += to_string(idx);
    Segment seg{segName, type, addr};
    debug("Found new segment: " + seg.toString());
    segments.push_back(seg);
}

void Executable::diffContext(const Context &ctx) const {
    const int CONTEXT_COUNT = ctx.options.ctxCount;
    Address a1 = ctx.refCsip; 
    Address a2 = ctx.tgtCsip;
    const Memory &code2 = ctx.target.code;
    const Block &ext2 = ctx.target.codeExtents;
    verbose("--- Context information for up to " + to_string(CONTEXT_COUNT) + " additional instructions after mismatch location:");
    Instruction i1, i2;
    for (int i = 0; i <= CONTEXT_COUNT; ++i) {
        // make sure we are within code extents in both executables
        if (!codeExtents.contains(a1) || !ext2.contains(a2)) break;
        i1 = Instruction{a1, code.pointer(a1)},
        i2 = Instruction{a2, code2.pointer(a2)};
        if (i != 0) verbose(compareStatus(i1, i2, true));
        a1 += i1.length;
        a2 += i2.length;
    }
}

// display skipped instructions after a definite match or mismatch found, impossible to display as instructions are scanned because we don't know
// how many will be skipped in advance
void Executable::skipContext(const Context &ctx, Address refAddr, Address tgtAddr, Size refSkipped, Size tgtSkipped) const {
    while (refSkipped > 0 || tgtSkipped > 0) {
        Instruction refInstr, tgtInstr;
        if (refSkipped > 0) {
            refInstr = {refAddr, code.pointer(refAddr)};
            refSkipped--;
            refAddr += refInstr.length;
        }
        if (tgtSkipped > 0) {
            tgtInstr = {tgtAddr, ctx.target.code.pointer(tgtAddr)};
            tgtSkipped--;
            tgtAddr += tgtInstr.length;
        }
        verbose(output_color(OUT_YELLOW) + compareStatus(refInstr, tgtInstr, true, INS_MATCH_DIFF) + " [skip]" + output_color(OUT_DEFAULT));
    }
}

// TODO: this should be a member of SearchQueue
bool Executable::saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents, ScanQueue &sq) const {
    if (!branch.destination.isValid())
        return false;

    if (codeExtents.contains(branch.destination)) {
        bool ret;
        if (branch.isCall) 
            ret = sq.saveCall(branch.destination, regs, branch.isNear); 
        else 
            ret = sq.saveJump(branch.destination, regs);
        return ret;
    }
    else {
        searchMessage(branch.source, "branch destination outside code boundaries: "s + branch.destination.toString());
    }
    return false; 
}

// explore the code without actually executing instructions, discover routine boundaries
// TODO: identify routines through signatures generated from OMF libraries
// TODO: trace usage of bp register (sub/add) to determine stack frame size of routines
// TODO: store references to potential jump tables (e.g. jmp cs:[bx+0xc08]), if unclaimed after initial search, try treating entries as pointers and run second search before coalescing blocks?
RoutineMap Executable::findRoutines() {
    RegisterState initRegs{entrypoint(), stack};
    storeSegment(Segment::SEG_STACK, stack.segment);
    debug("initial register values:\n"s + initRegs.toString());
    // queue for BFS search
    ScanQueue searchQ{Destination(entrypoint(), 1, true, initRegs)};
    info("Analyzing code within extents: "s + codeExtents);

    // iterate over entries in the search queue
    while (!searchQ.empty()) {
        // get a location from the queue and jump to it
        const Destination search = searchQ.nextPoint();
        Address csip = search.address;
        searchMessage(csip, "--- starting search at new location for routine " + to_string(search.routineId) + ", call: "s + to_string(search.isCall) + ", queue = " + to_string(searchQ.size()));
        RegisterState regs = search.regs;
        regs.setValue(REG_CS, csip.segment);
        storeSegment(Segment::SEG_CODE, csip.segment);
        // iterate over instructions at current search location in a linear fashion, until an unconditional jump or return is encountered
        while (true) {
            if (!codeExtents.contains(csip))
                throw AnalysisError("Advanced past loaded code extents: "s + csip.toString());
            // check if this location was visited before
            const auto rid = searchQ.getRoutineId(csip.toLinear());
            if (rid != NULL_ROUTINE) {
                // make sure we do not steamroll over previously explored instructions from a wild jump (a call has precedence)
                if (!search.isCall) {
                    searchMessage(csip, "location already claimed by routine "s + to_string(rid) + " and search point did not originate from a call, halting scan");
                    break;
                }
            }
            const auto isEntry = searchQ.isEntrypoint(csip);
            // similarly, protect yet univisited locations which are however recognized as routine entrypoints, unless visiting from a matching routine id
            if (isEntry != NULL_ROUTINE && isEntry != search.routineId) {
                searchMessage(csip, "location marked as entrypoint for routine "s + to_string(isEntry) + " while scanning from " + to_string(search.routineId) + ", halting scan");
                break;
            }
            Instruction i(csip, code.pointer(csip));
            regs.setValue(REG_IP, csip.offset);
            // mark memory map items corresponding to the current instruction as belonging to the current routine
            searchQ.setRoutineId(csip.toLinear(), i.length);
            // interpret the instruction
            if (i.isBranch()) {
                const Branch branch = getBranch(i, regs);
                // if the destination of the branch can be established, place it in the search queue
                saveBranch(branch, regs, codeExtents, searchQ);
                // even if the branch destination is not known, we cannot keep scanning here if it was unconditional
                if (branch.isUnconditional) {
                    searchMessage(csip, "routine scan interrupted by unconditional branch");
                    break;
                }
            }
            else if (i.isReturn()) {
                // TODO: check if return matches entrypoint type (near/far), warn otherwise
                searchMessage(csip, "routine scan interrupted by return");
                break;
            }
            else if (i.iclass == INS_MOV) {
                applyMov(i, regs);
            }
            else {
                for (Register r : i.touchedRegs()) {
                    regs.setUnknown(r);
                }
            }
            // advance to next instruction
            csip += i.length;
        } // next instructions
    } // next address from search queue
    info("Done analyzing code");
    // XXX: debug, remove
    searchQ.dumpVisited("routines.visited", SEG_TO_OFFSET(loadSegment), codeSize);

    // iterate over discovered memory map and create routine map
    auto ret = RoutineMap{searchQ, segments, loadSegment, codeSize};
    return ret;
}

// TODO: move this out of Executable, will help with unifying how both executables are referenced inside
bool Executable::compareCode(const RoutineMap &routineMap, const Executable &target, const AnalysisOptions &options) {
    verbose("Comparing code between reference (entrypoint "s + entrypoint().toString() + ") and target (entrypoint " + target.entrypoint().toString() + ") executables");
    debug("Routine map of reference binary has " + to_string(routineMap.size()) + " entries");
    Context ctx{target, options, routineMap.segmentCount(Segment::SEG_DATA)};
    // map of equivalent addresses in the compared binaries, seed with the two entrypoints
    ctx.offMap.setCode(entrypoint(), target.entrypoint());
    const RoutineId VISITED_ID = 1;
    // queue of locations (in reference binary) for comparison, likewise seeded with the entrypoint
    // TODO: use routine map to fill compareQ in advance
    // TODO: implement register value tracing
    ScanQueue compareQ{Destination(entrypoint(), VISITED_ID, true, {})};
    std::regex excludeRe{options.exclude};
    while (!compareQ.empty()) {
        // get next location for linear scan and comparison of instructions from the front of the queue,
        // to visit functions in the same order in which they were first encountered
        const Destination compare = compareQ.nextPoint();
        debug("Now at reference location "s + compare.address.toString() + ", queue size = " + to_string(compareQ.size()));
        // when entering a routine, forget all the current stack offset mappings
        if (compare.isCall) {
            ctx.offMap.resetStack();
        }
        ctx.refCsip = compare.address;
        if (options.stopAddr.isValid() && ctx.refCsip >= options.stopAddr) {
            verbose("Reached stop address: " + ctx.refCsip.toString());
            goto success;
        }        
        if (compareQ.getRoutineId(ctx.refCsip.toLinear()) != NULL_ROUTINE) {
            debug("Location already compared, skipping");
            continue;
        }
        // get corresponding address in target binary
        ctx.tgtCsip = ctx.offMap.getCode(ctx.refCsip);
        if (!ctx.tgtCsip.isValid()) {
            error("Could not find equivalent address for "s + ctx.refCsip.toString() + " in address map for target executable");
            return false;
        }
        Routine routine{"unknown", {}};
        Block compareBlock;
        if (!routineMap.empty()) { // comparing with a map
            routine = routineMap.getRoutine(ctx.refCsip);
            // make sure we are inside a reachable block of a know routine from reference binary
            if (!routine.isValid()) {
                error("Could not find address "s + ctx.refCsip.toString() + " in routine map");
                return false;
            }
            compareBlock = routine.blockContaining(compare.address);
            if (!options.exclude.empty() && std::regex_match(routine.name, excludeRe)) {
                verbose("--- Skipping excluded routine " + routine.toString(false) + " @"s + ctx.refCsip.toString() + ", block " + compareBlock.toString(true) +  ", target @" + ctx.tgtCsip.toString());
                continue;
            }
            verbose("--- Now @"s + ctx.refCsip.toString() + ", routine " + routine.toString(false) + ", block " + compareBlock.toString(true) +  ", target @" + ctx.tgtCsip.toString());
        }
        // TODO: consider dropping this "feature"
        else { // comparing without a map
            verbose("--- Comparing reference @ "s + ctx.refCsip.toString() + " to target @" + ctx.tgtCsip.toString());
        }
        Size refSkipCount = 0, tgtSkipCount = 0;
        Address refSkipOrigin, tgtSkipOrigin;

        // keep comparing subsequent instructions at current location between the reference and target binary
        while (true) {
            // if we ran outside of the code extents, consider the comparison successful
            if (!contains(ctx.refCsip) || !target.contains(ctx.tgtCsip)) {
                debug("Advanced past code extents: csip = "s + ctx.refCsip.toString() + " / " + ctx.tgtCsip.toString() + " vs extents " + codeExtents.toString() 
                    + " / " + target.codeExtents.toString());
                // make sure we are not skipping instructions
                // TODO: make this non-fatal, just make the skip fail
                if (refSkipCount || tgtSkipCount) return false;
                else break;
            }

            // decode instructions
            Instruction 
                refInstr{ctx.refCsip, code.pointer(ctx.refCsip)}, 
                tgtInstr{ctx.tgtCsip, target.code.pointer(ctx.tgtCsip)};
            
            // mark this instruction as visited, unlike the routine finding algorithm, we do not differentiate between routine IDs
            compareQ.setRoutineId(ctx.refCsip.toLinear(), refInstr.length, VISITED_ID);

            // compare instructions
            SkipType skipType = SKIP_NONE;
            const auto matchType = instructionsMatch(ctx, refInstr, tgtInstr);
            switch (matchType) {
            case CMP_MATCH:
                // display skipped instructions if there were any before this match
                if (refSkipCount || tgtSkipCount) {
                    skipContext(ctx, refSkipOrigin, tgtSkipOrigin, refSkipCount, tgtSkipCount);
                    refSkipCount = tgtSkipCount = 0;
                    refSkipOrigin = tgtSkipOrigin = Address();
                }
                verbose(compareStatus(refInstr, tgtInstr, true));
                // an instruction match resets the allowed skip counters
                break;
            case CMP_MISMATCH:
                // attempt to skip a mismatch, if permitted by the options
                // first try skipping in the reference executable, skipping returns is not allowed
                if (refSkipCount + 1 <= options.refSkip && !refInstr.isReturn()) {
                    // save original reference position for rewind
                    if (refSkipCount == 0) refSkipOrigin = ctx.refCsip;
                    skipType = SKIP_REF;
                    refSkipCount++;
                }
                // no more skips allowed in reference, try skipping target executable
                else if (tgtSkipCount + 1 <= options.tgtSkip && !tgtInstr.isReturn()) {
                    // save original target position for skip context display
                    if (tgtSkipCount == 0) tgtSkipOrigin = ctx.tgtCsip;
                    skipType = SKIP_TGT;
                    // performing a skip in the target executable resets the allowed reference skip count
                    refSkipCount = 0;
                    tgtSkipCount++;
                }
                // ran out of allowed skips in both executables
                else {
                    // display skipped instructions if there were any before this mismatch
                    if (refSkipCount || tgtSkipCount) {
                        skipContext(ctx, refSkipOrigin, tgtSkipOrigin, refSkipCount, tgtSkipCount);
                    }
                    verbose(output_color(OUT_RED) + compareStatus(refInstr, tgtInstr, true) + output_color(OUT_DEFAULT));
                    error("Instruction mismatch in routine " + routine.name + " at " + compareStatus(refInstr, tgtInstr, false));
                    diffContext(ctx);
                    return false;
                }
                break;
            case CMP_DIFFVAL:
                verbose(output_color(OUT_YELLOW) + compareStatus(refInstr, tgtInstr, true) + output_color(OUT_DEFAULT));
                break;
            }
            // comparison result okay (instructions match or skip permitted), interpret the instructions

            // instruction is a call, save destination to the comparison queue 
            if (!options.noCall && refInstr.isCall() && tgtInstr.isCall()) {
                const Branch 
                    refBranch = getBranch(refInstr, {}),
                    tgtBranch = getBranch(tgtInstr, {});
                // if the destination of the branch can be established, place it in the compare queue
                if (saveBranch(refBranch, {}, codeExtents, compareQ)) {
                    // if the branch destination was accepted, save the address mapping of the branch destination between the reference and target
                    ctx.offMap.codeMatch(refBranch.destination, tgtBranch.destination);
                }
            }
            // instruction is a jump, save the relationship between the reference and the target addresses into the offset map
            else if (refInstr.isJump() && tgtInstr.isJump() && skipType == SKIP_NONE) {
                const Branch 
                    refBranch = getBranch(refInstr, {}),
                    tgtBranch = getBranch(tgtInstr, {});
                if (refBranch.destination.isValid() && tgtBranch.destination.isValid()) {
                    ctx.offMap.codeMatch(refBranch.destination, tgtBranch.destination);
                }
            }

            // adjust position in compared executables for next iteration
            switch (matchType) {
            case CMP_MISMATCH:
                // if the instructions did not match and we still got here, that means we are in difference skipping mode, 
                debug(compareStatus(refInstr, tgtInstr, false, INS_MATCH_MISMATCH));
                switch (skipType) {
                case SKIP_REF: 
                    ctx.refCsip += refInstr.length;
                    debug("Skipping over reference instruction mismatch, allowed " + to_string(refSkipCount) + " out of " + to_string(options.refSkip) + ", destination " + ctx.refCsip.toString());
                    break;
                case SKIP_TGT:
                    // rewind reference position if it was skipped before
                    if (refSkipOrigin.isValid()) ctx.refCsip = refSkipOrigin;
                    ctx.tgtCsip += tgtInstr.length;
                    debug("Skipping over target instruction mismatch, allowed " + to_string(tgtSkipCount)  + " out of " + to_string(options.tgtSkip) + ", destination " + ctx.tgtCsip.toString());
                    break;
                default:
                    error("Unexpected: no skip despite mismatch");
                    return false;
                }
                break;
            case CMP_VARIANT:
                ctx.refCsip += refInstr.length;
                // in case of a variant match, the instruction pointer in the target binary will have already been advanced by instructionsMatch()
                debug("Variant match detected, comparison will continue at " + ctx.tgtCsip.toString());
                break;
            default:
                // normal case, advance both reference and target positions
                ctx.refCsip += refInstr.length;
                ctx.tgtCsip += tgtInstr.length;
                break;
            }
            
            // an end of a routine block is a hard stop, we do not want to go into unreachable areas which may contain invalid opcodes (e.g. data mixed with code)
            // TODO: need to handle case where we are skipping right now
            if (compareBlock.isValid() && ctx.refCsip > compareBlock.end) {
                verbose("Reached end of routine block @ "s + compareBlock.end.toString());
                // if the current routine still contains reachable blocks after the current location, add the start of the next one to the back of the queue,
                // so it gets picked up immediately on the next iteration of the outer loop
                const Block rb = routine.nextReachable(ctx.refCsip);
                if (rb.isValid()) {
                    verbose("Routine still contains reachable blocks, next @ " + rb.toString());
                    compareQ.saveJump(rb.begin, {});
                    if (!ctx.offMap.getCode(rb.begin).isValid()) {
                        // the offset map between the reference and the target does not have a matching entry for the next reachable block's destination,
                        // so the comparison would fail - last ditch attempt is to try and record a "guess" offset mapping before jumping there,
                        // based on the hope that the size of the unreachable block matches in the target
                        // TODO: make sure an instruction matches at the destination before actually recording the mapping
                        const Address guess = ctx.tgtCsip + (rb.begin - ctx.refCsip);
                        debug("Recording guess offset mapping based on unreachable block size: " + rb.begin.toString() + " -> " + guess.toString());
                        ctx.offMap.setCode(rb.begin, guess);
                    }
                }
                else {
                    verbose("Completed comparison of routine " + routine.name + ", no more reachable blocks");
                }
                break;
            }

            // reached predefined stop address
            if (options.stopAddr.isValid() && ctx.refCsip >= options.stopAddr) {
                verbose("Reached stop address: " + ctx.refCsip.toString());
                goto success;
            }
        } // iterate over instructions at current comparison location
    } // iterate over comparison location queue

success:
    verbose(output_color(OUT_GREEN) + "Comparison result positive" + output_color(OUT_DEFAULT));
    return true;
}
