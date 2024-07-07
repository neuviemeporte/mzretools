#include "dos/analysis.h"
#include "dos/opcodes.h"
#include "dos/output.h"
#include "dos/util.h"
#include "dos/error.h"
#include "dos/executable.h"

#include <iostream>
#include <istream>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <string>
#include <cstring>

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

void searchMessage(const Address &addr, const string &msg) {
    output(addr.toString() + ": " + msg, LOG_ANALYSIS, LOG_DEBUG);
}

string Destination::toString() const {
    ostringstream str;
    str << "[" << address.toString() << " / " << routineId << " / " << (isCall ? "call" : "jump") << "]";
    return str.str();
}

ScanQueue::ScanQueue(const Destination &seed) : 
    visited(MEM_TOTAL, NULL_ROUTINE),
    start(seed.address)
{
    queue.push_front(seed);
    entrypoints.emplace_back(RoutineEntrypoint(start, seed.routineId, true));
}

string ScanQueue::statusString() const { 
    return "[r"s + to_string(curSearch.routineId) + "/q" + to_string(size()) + "]"; 
} 

RoutineId ScanQueue::getRoutineId(Offset off) const { 
    assert(off < visited.size());
    return visited.at(off); 
}

void ScanQueue::setRoutineId(Offset off, const Size length, RoutineId id) {
    if (id == NULL_ROUTINE) id = curSearch.routineId;
    assert(off < visited.size());
    fill(visited.begin() + off, visited.begin() + off + length, id);
}

Destination ScanQueue::nextPoint() {
    if (!empty()) {
        curSearch = queue.front();
        queue.pop_front();
    }
    return curSearch;
}

bool ScanQueue::hasPoint(const Address &dest, const bool call) const {
    Destination findme(dest, 0, call, RegisterState());
    const auto &it = std::find_if(queue.begin(), queue.end(), [&](const Destination &p){
        return p.match(findme);
    });
    return it != queue.end();
};

RoutineId ScanQueue::isEntrypoint(const Address &addr) const {
    const auto &found = std::find(entrypoints.begin(), entrypoints.end(), addr);
    if (found != entrypoints.end()) return found->id;
    else return NULL_ROUTINE;
}

// return the set of routines found by the queue, these will only have the entrypoint set and an automatic name generated
vector<Routine> ScanQueue::getRoutines() const {
    auto routines = vector<Routine>{routineCount()};
    for (const auto &ep : entrypoints) {
        auto &r = routines.at(ep.id - 1);
        // initialize routine extents with entrypoint address
        r.extents = Block(ep.addr);
        r.near = ep.near;
        // assign automatic names to routines
        if (ep.addr == startAddress()) r.name = "start";
        else r.name = "routine_"s + to_string(ep.id);
    }
    return routines;
}

// function call, create new routine at destination if either not visited, 
// or visited but destination was not yet discovered as a routine entrypoint and this call now takes precedence and will replace it
bool ScanQueue::saveCall(const Address &dest, const RegisterState &regs, const bool near) {
    const RoutineId destId = getRoutineId(dest.toLinear());
    if (isEntrypoint(dest)) 
        debug("Address "s + dest.toString() + " already registered as entrypoint for routine " + to_string(destId));
    else if (hasPoint(dest, true)) 
        debug("Search queue already contains call to address "s + dest.toString());
    else { // not a known entrypoint and not yet in queue
        Size newRoutineId = routineCount() + 1;
        if (destId == NULL_ROUTINE)
            debug("call destination not belonging to any routine, claiming as entrypoint for new routine " + to_string(newRoutineId));
        else 
            debug("call destination belonging to routine " + to_string(destId) + ", reclaiming as entrypoint for new routine " + to_string(newRoutineId));
        queue.emplace_back(Destination(dest, newRoutineId, true, regs));
        entrypoints.emplace_back(RoutineEntrypoint(dest, newRoutineId, near));
        return true;
    }
    return false;
}

// conditional jump, save as destination to be investigated, belonging to current routine
bool ScanQueue::saveJump(const Address &dest, const RegisterState &regs) {
    const RoutineId destId = getRoutineId(dest.toLinear());
    if (destId != NULL_ROUTINE) 
        debug("Jump destination already visited from routine "s + to_string(destId));
    else if (hasPoint(dest, false))
        debug("Queue already contains jump to address "s + dest.toString());
    else { // not claimed by any routine and not yet in queue
        debug("Jump destination not yet visited, scheduled visit from routine " + to_string(curSearch.routineId) + ", queue size = " + to_string(size()));
        queue.emplace_front(Destination(dest, curSearch.routineId, false, regs));
        return true;
    }
    return false;
}

bool ScanQueue::saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents) {
    if (!branch.destination.isValid())
        return false;

    if (codeExtents.contains(branch.destination)) {
        bool ret;
        if (branch.isCall) 
            ret = saveCall(branch.destination, regs, branch.isNear); 
        else 
            ret = saveJump(branch.destination, regs);
        return ret;
    }
    else {
        searchMessage(branch.source, "branch destination outside code boundaries: "s + branch.destination.toString());
    }
    return false; 
}


// This is very slow!
void ScanQueue::dumpVisited(const string &path, const Offset start, Size size) const {
    // dump map to file for debugging
    ofstream mapFile(path);
    mapFile << "      ";
    for (int i = 0; i < 16; ++i) mapFile << hex << setw(5) << setfill(' ') << i << " ";
    mapFile << endl;
    if (size == 0) size = visited.size();
    info("Dumping visited map of size "s + hexVal(size) + " starting at " + hexVal(start) + " to " + path);
    for (Offset mapOffset = start; mapOffset < start + size; ++mapOffset) {
        const auto id = getRoutineId(mapOffset);
        //debug(hexVal(mapOffset) + ": " + to_string(m));
        const Offset printOffset = mapOffset - start;
        if (printOffset % 16 == 0) {
            if (printOffset != 0) mapFile << endl;
            mapFile << hex << setw(5) << setfill('0') << printOffset << " ";
        }
        mapFile << dec << setw(5) << setfill(' ') << id << " ";
    }
}

bool OffsetMap::codeMatch(const Address from, const Address to) {
    if (!(from.isValid() && to.isValid()))
        return false;

    // mapping already exists
    if (codeMap.count(from) > 0) {
        auto &found = codeMap[from];
        if (found == to) {
            debug("Existing code address mapping " + from.toString() + " -> " + to.toString() + " matches");
            return true;
        }
        debug("Existing code address mapping " + from.toString() + " -> " + found.toString() + " conflicts with " + to.toString());
        return false;
    }
    // otherwise save new mapping
    debug("Registering new code address mapping: " + from.toString() + " -> " + to.toString());
    codeMap[from] = to;
    return true;    
}

bool OffsetMap::dataMatch(const SOffset from, const SOffset to) {
    auto &mappings = dataMap[from];
    // matching mapping already exists in map
    if (std::find(begin(mappings), end(mappings), to) != mappings.end()) {
        debug("Existing data offset mapping " + hexVal(from) + " -> " + dataStr(mappings) + " matches");
        return true;
    }
    // mapping does not exist, but still room left, so save it and carry on
    else if (mappings.size() < maxData) {
        debug("Registering new data offset mapping: " + hexVal(from) + " -> " + hexVal(to));
        mappings.push_back(to);
        return true;
    }
    // no matching mapping and limit already reached
    else {
        debug("Existing data offset mapping " + hexVal(from) + " -> " + dataStr(mappings) + " conflicts with " + hexVal(to));
        return false;
    }
}

bool OffsetMap::stackMatch(const SOffset from, const SOffset to) {
    // mapping already exists
    if (stackMap.count(from) > 0) {
        if (stackMap[from] == to) {
            debug("Existing stack offset mapping " + hexVal(from) + " -> " + hexVal(to) + " matches");
            return true;
        }
        return false;
    }
    // otherwise save new mapping
    debug("Registering new stack offset mapping: " + hexVal(from) + " -> " + hexVal(to));
    stackMap[from] = to;
    return true;
}

std::string OffsetMap::dataStr(const MapSet &ms) const {
    ostringstream str;
    int idx = 0;
    for (SOffset o : ms) {
        if (idx != 0) str << "/";
        str << hexVal(o);
    }
    return str.str();
}

VariantMap::VariantMap() {
}

VariantMap::VariantMap(std::istream &str) {
    loadFromStream(str);
}

VariantMap::VariantMap(const std::string &path) {
    auto stat = checkFile(path);
    if (!stat.exists) throw ArgError("Variant map file does not exist: " + path);
    ifstream file{path};
    loadFromStream(file);
}

VariantMap::MatchDepth VariantMap::checkMatch(const VariantMap::Variant &left, const VariantMap::Variant &right) {
    if (left.empty() || right.empty()) throw ArgError("Empty argument to variant match check");
    // check to see if there is a bucket containing the left (reference) variant, i.e. whether there is a variant of this instruction or sequence of instructions
    int bucketIdx, leftFound = 0;
    for (bucketIdx = 0; bucketIdx < buckets_.size(); ++bucketIdx) {
        if ((leftFound = find(left, bucketIdx)) != 0) break;
    }
    if (leftFound == 0) return {}; // left instruction has no known variants, return no match
    // try to find the right instruction or sequence of instructions in the same bucket
    int rightFound = find(right, bucketIdx);
    if (rightFound == 0) return {}; // right instruction has no variant registered in the same bucket as the left instruction, return no match
    return {leftFound, rightFound}; // return positive match with corresponding instruction sequence lengths
}

void VariantMap::dump() const {
    debug("variant map, max depth = " + to_string(maxDepth()));
    int bucketno = 0;
    for (const auto &bucket : buckets_) {
        debug("bucket " + to_string(bucketno) + ": ");
        int variantno = 0;
        for (const auto &variant : bucket) {
            ostringstream varstr;
            varstr << "variant " << variantno << ": ";
            int instructionno = 0;
            for (const auto &instruction : variant) {
                if (instructionno > 0) varstr << " - ";
                varstr << "'" << instruction << "'";
                instructionno++;
            }
            debug(varstr.str());
            variantno++;
        }
        bucketno++;
    }
}

// search a specific bucket for a match against a variant search, return the number of instructions that a matching variant was found having, or zero if none was found
int VariantMap::find(const Variant &search, int bucketno) const {
    debug("looking for search item of size " + to_string(search.size()) + " in bucket " + to_string(bucketno));
    const auto &bucket = buckets_[bucketno];
    for (const auto &variant : bucket) {
        debug("matching against variant of size " + to_string(variant.size()));
        if (search.size() < variant.size()) {
            debug("search item too small (" + to_string(search.size()) + ") to check against variant (" + to_string(variant.size()) + ")");
            continue;
        }
        size_t searchIdx = 0;
        bool match = true;
        for (const auto &instrStr : variant) {
            if (search[searchIdx++] != instrStr) { 
                debug("failed comparison on index " + to_string(searchIdx-1) + ": '" + search[searchIdx-1] + "' != '" + instrStr + "'");
                match = false; 
                break; 
            } 
        }
        if (match) {
            debug("matched with size " + to_string(variant.size()));
            return variant.size();
        }
    }
    return 0;
}

void VariantMap::loadFromStream(std::istream &str) {
    string bucketLine;
    Size maxDepth = 0;
    // iterate over lines, each is a bucket of equivalent instruction variants
    while (safeGetline(str, bucketLine)) {
        // skip over empty and comment lines
        if (bucketLine.empty() || bucketLine[0] == '#') continue;
        Bucket bucket;
        // iterate over slash-separated strings on a line, each is a variant
        for (const auto &variantString : splitString(bucketLine, '/')) {
            // a variant starts and ends with a quote
            Variant variant = splitString(variantString, ';');
            if (variant.empty()) throw ParseError("Empty variant string: " + variantString);
            if (variant.size() > maxDepth) maxDepth = variant.size();
            bucket.push_back(variant);
        }
        if (!bucket.empty()) buckets_.push_back(bucket);
    }
    maxDepth_ = maxDepth;
}

static void applyMov(const Instruction &i, RegisterState &regs, Executable &exe) {
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
        auto &code = exe.getCode();
        Address srcAddr(regs.getValue(segReg), i.op2.immval.u16);
        if (exe.contains(srcAddr)) {
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
        if (i.op1.type == OPR_REG_DS) exe.storeSegment(Segment::SEG_DATA, regs.getValue(REG_DS));
        else if (i.op1.type == OPR_REG_SS) exe.storeSegment(Segment::SEG_STACK, regs.getValue(REG_SS));
    }    
}


// explore the code without actually executing instructions, discover routine boundaries
// TODO: identify routines through signatures generated from OMF libraries
// TODO: trace usage of bp register (sub/add) to determine stack frame size of routines
// TODO: store references to potential jump tables (e.g. jmp cs:[bx+0xc08]), if unclaimed after initial search, try treating entries as pointers and run second search before coalescing blocks?
RoutineMap Analyzer::findRoutines(Executable &exe) {
    RegisterState initRegs{exe.entrypoint(), exe.stackAddr()};
    
    debug("initial register values:\n"s + initRegs.toString());
    // queue for BFS search
    ScanQueue searchQ{Destination(exe.entrypoint(), 1, true, initRegs)};
    info("Analyzing code within extents: "s + exe.extents());
 
    // iterate over entries in the search queue
    while (!searchQ.empty()) {
        // get a location from the queue and jump to it
        const Destination search = searchQ.nextPoint();
        Address csip = search.address;
        searchMessage(csip, "--- starting search at new location for routine " + to_string(search.routineId) + ", call: "s + to_string(search.isCall) + ", queue = " + to_string(searchQ.size()));
        RegisterState regs = search.regs;
        regs.setValue(REG_CS, csip.segment);
        exe.storeSegment(Segment::SEG_CODE, csip.segment);
        // iterate over instructions at current search location in a linear fashion, until an unconditional jump or return is encountered
        while (true) {
            if (!exe.contains(csip))
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
            Instruction i(csip, exe.codePointer(csip));
            regs.setValue(REG_IP, csip.offset);
            // mark memory map items corresponding to the current instruction as belonging to the current routine
            searchQ.setRoutineId(csip.toLinear(), i.length);
            // interpret the instruction
            if (i.isBranch()) {
                const Branch branch = getBranch(i, regs);
                // if the destination of the branch can be established, place it in the search queue
                searchQ.saveBranch(branch, regs, exe.extents());
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
                applyMov(i, regs, exe);
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
#ifdef DEBUG
    searchQ.dumpVisited("routines.visited", SEG_TO_OFFSET(loadSegment), codeSize);
#endif

    // iterate over discovered memory map and create routine map
    auto ret = RoutineMap{searchQ, exe.getSegments(), exe.getLoadSegment(), exe.size()};
    // TODO: stats like for compare
    return ret;
}


bool Analyzer::compareCode(const Executable &ref, const Executable &tgt, const RoutineMap &refMap, const RoutineMap &tgtMap) {
    verbose("Comparing code between reference (entrypoint "s + ref.entrypoint().toString() + ") and target (entrypoint " + tgt.entrypoint().toString() + ") executables");
    debug("Routine map of reference binary has " + to_string(refMap.size()) + " entries");
    Context ctx{target, options, refMap.segmentCount(Segment::SEG_DATA), entrypoint()};
    compareQ = {Destination(entrypoint, VISITED_ID, true, {})};
    // map of equivalent addresses in the compared binaries, seed with the two entrypoints
    ctx.offMap.setCode(entrypoint(), tgt.entrypoint());
    // queue of locations (in reference binary) for comparison, likewise seeded with the entrypoint
    // TODO: use routine map to fill compareQ in advance
    // TODO: implement register value tracing
    while (!ctx.compareQ.empty()) {
        // get next location for linear scan and comparison of instructions from the front of the queue,
        // to visit functions in the same order in which they were first encountered
        const Destination compare = ctx.compareQ.nextPoint();
        debug("Now at reference location "s + compare.address.toString() + ", queue size = " + to_string(ctx.compareQ.size()));
        // when entering a routine, forget all the current stack offset mappings
        if (compare.isCall) {
            ctx.offMap.resetStack();
        }
        ctx.refCsip = compare.address;
        if (options.stopAddr.isValid() && ctx.refCsip >= options.stopAddr) {
            verbose("Reached stop address: " + ctx.refCsip.toString());
            goto success;
        }        
        if (ctx.compareQ.getRoutineId(ctx.refCsip.toLinear()) != NULL_ROUTINE) {
            debug("Location already compared, skipping");
            continue;
        }
        // get corresponding address in target binary
        ctx.tgtCsip = ctx.offMap.getCode(ctx.refCsip);
        if (!ctx.tgtCsip.isValid()) {
            error("Could not find equivalent address for "s + ctx.refCsip.toString() + " in address map for target executable");
            return false;
        }
        ctx.routine = {"unknown", {}};
        Size routineCount = 0;
        Block compareBlock;
        if (!refMap.empty()) { // comparing with a map
            ctx.routine = refMap.getRoutine(ctx.refCsip);
            // make sure we are inside a reachable block of a know routine from reference binary
            if (!ctx.routine.isValid()) {
                error("Could not find address "s + ctx.refCsip.toString() + " in routine map");
                return false;
            }
            ctx.routineNames.insert(ctx.routine.name);
            compareBlock = ctx.routine.blockContaining(compare.address);
            if (ctx.routine.ignore || (ctx.routine.assembly && !ctx.options.checkAsm)) {
                verbose("--- Skipping excluded routine " + ctx.routine.toString(false) + " @"s + ctx.refCsip.toString() + ", block " + compareBlock.toString(true) +  ", target @" + ctx.tgtCsip.toString());
                ctx.excludedNames.insert(ctx.routine.name);
                continue;
            }
            verbose("--- Now @"s + ctx.refCsip.toString() + ", routine " + ctx.routine.toString(false) + ", block " + compareBlock.toString(true) +  ", target @" + ctx.tgtCsip.toString());
        }
        // TODO: consider dropping this "feature"
        else { // comparing without a map
            verbose("--- Comparing reference @ "s + ctx.refCsip.toString() + " to target @" + ctx.tgtCsip.toString());
        }

        // keep comparing subsequent instructions from current search queue location between the reference and target binary
        if (!comparisonLoop()) return false;

    } // iterate over comparison location queue

success:
    verbose(output_color(OUT_GREEN) + "Comparison result: match" + output_color(OUT_DEFAULT));
    compareSummary(ctx, routineMap, true);
    return true;    
}

bool Analyzer::comparisonLoop() {
    Size refSkipCount = 0, tgtSkipCount = 0;
    Address refSkipOrigin, tgtSkipOrigin;
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
        ctx.compareQ.setRoutineId(ctx.refCsip.toLinear(), refInstr.length, VISITED_ID);

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
            if (refSkipCount + 1 <= ctx.options.refSkip && !refInstr.isReturn()) {
                // save original reference position for rewind
                if (refSkipCount == 0) refSkipOrigin = ctx.refCsip;
                skipType = SKIP_REF;
                refSkipCount++;
            }
            // no more skips allowed in reference, try skipping target executable
            else if (tgtSkipCount + 1 <= ctx.options.tgtSkip && !tgtInstr.isReturn()) {
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
                error("Instruction mismatch in routine " + ctx.routine.name + " at " + compareStatus(refInstr, tgtInstr, false));
                diffContext(ctx);
                compareSummary(ctx.comparedSize, routineMap, ctx.routineNames, excludedNames, options, false);
                return false;
            }
            break;
        case CMP_DIFFVAL:
            verbose(output_color(OUT_YELLOW) + compareStatus(refInstr, tgtInstr, true) + output_color(OUT_DEFAULT));
            break;
        case CMP_DIFFTGT:
            verbose(output_color(OUT_BRIGHTRED) + compareStatus(refInstr, tgtInstr, true) + output_color(OUT_DEFAULT));
            break;                
        }
        // comparison result okay (instructions match or skip permitted), interpret the instructions

        // instruction is a call, save destination to the comparison queue 
        if (!ctx.options.noCall && refInstr.isCall() && tgtInstr.isCall()) {
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
        // TODO: isn't this already handled in instructionsMatch()?
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
                ctx.comparedSize += refInstr.length;
                ctx.refCsip += refInstr.length;
                debug("Skipping over reference instruction mismatch, allowed " + to_string(refSkipCount) + " out of " + to_string(ctx.options.refSkip) + ", destination " + ctx.refCsip.toString());
                break;
            case SKIP_TGT:
                // rewind reference position if it was skipped before
                if (refSkipOrigin.isValid()) {
                    ctx.comparedSize -= ctx.refCsip.offset - refSkipOrigin.offset;
                    ctx.refCsip = refSkipOrigin;
                }
                ctx.tgtCsip += tgtInstr.length;
                debug("Skipping over target instruction mismatch, allowed " + to_string(tgtSkipCount)  + " out of " + to_string(ctx.options.tgtSkip) + ", destination " + ctx.tgtCsip.toString());
                break;
            default:
                error("Unexpected: no skip despite mismatch");
                return false;
            }
            break;
        case CMP_VARIANT:
            ctx.comparedSize += refInstr.length;
            ctx.refCsip += refInstr.length;
            // in case of a variant match, the instruction pointer in the target binary will have already been advanced by instructionsMatch()
            debug("Variant match detected, comparison will continue at " + ctx.tgtCsip.toString());
            break;
        default:
            // normal case, advance both reference and target positions
            ctx.comparedSize += refInstr.length;
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
            const Block rb = ctx.routine.nextReachable(ctx.refCsip);
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
                verbose("Completed comparison of routine " + ctx.routine.name + ", no more reachable blocks");
            }
            break;
        }

        // reached predefined stop address
        if (ctx.options.stopAddr.isValid() && ctx.refCsip >= ctx.options.stopAddr) {
            verbose("Reached stop address: " + ctx.refCsip.toString());
            break;
        }
    } // iterate over instructions at current comparison location    
    return true;
}

Branch Analyzer::getBranch(const Instruction &i, const RegisterState &regs) const {
    Branch branch;
    branch.isCall = false;
    branch.isUnconditional = false;
    branch.isNear = true;
    const Address &addr = i.addr;
    switch (i.iclass) {
    // conditional jump
    case INS_JMP_IF:
        branch.destination = i.destinationAddress();
        searchMessage(addr, "encountered conditional near jump to "s + branch.destination.toString());
        break;
    // unconditional jumps
    case INS_JMP:
        switch (i.opcode) {
        case OP_JMP_Jb: 
        case OP_JMP_Jv: 
            branch.destination = i.destinationAddress();
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
        branch.destination = i.destinationAddress();
        searchMessage(addr, "encountered loop to "s + branch.destination.toString());
        break;
    // calls
    case INS_CALL:
        if (i.op1.type == OPR_IMM16) {
            branch.destination = i.destinationAddress();
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

Analyzer::ComparisonResult Analyzer::instructionsMatch(const Instruction &ref, Instruction tgt) {
    if (ctx.options.ignoreDiff) return CMP_MATCH;

    auto insResult = ref.match(tgt);
    if (insResult == INS_MATCH_FULL) return CMP_MATCH;

    bool match = false;
    // instructions differ in value of immediate or memory offset
    if (insResult == INS_MATCH_DIFF || insResult == INS_MATCH_DIFFOP1 || insResult == INS_MATCH_DIFFOP2) {
        if (ref.isBranch()) {
            const Branch 
                refBranch = getBranch(ref), 
                tgtObranch = getBranch(tgt);
            if (refBranch.destination.isValid() && tgtObranch.destination.isValid()) {
                match = ctx.offMap.codeMatch(refBranch.destination, tgtObranch.destination);
                if (!match) debug("Instruction mismatch on branch destination");
                // near jumps are usually used within a routine to handle looping and conditions,
                // so a different value (relative jump amount) might mean a wrong flow
                // -- mark with a different result value to be highlighted
                else if (ref.isNearJump()) return CMP_DIFFTGT;
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
        // iterate over operands, either one or both could be different, so check if the difference is acceptable
        for (int opidx = 1; opidx <= 2; ++opidx) {
            const Instruction::Operand *op = nullptr;
            switch(opidx) {
            case 1: if (insResult != INS_MATCH_DIFFOP2) op = &ref.op1; break;
            case 2: if (insResult != INS_MATCH_DIFFOP1) op = &ref.op2; break;
            }
            if (op == nullptr) continue;

            if (operandIsMemWithOffset(op->type)) {
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
            else if (operandIsImmediate(op->type) && !ctx.options.strict) {
                debug("Ignoring immediate value difference in loose mode");
                return CMP_DIFFVAL;
            }
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

void Analyzer::diffContext() const {
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
void Analyzer::skipContext(Address refAddr, Address tgtAddr, Size refSkipped, Size tgtSkipped) const {
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

// display comparison statistics
void Analyzer::compareSummary(const Context &ctx, const RoutineMap &routineMap, const bool showMissed) const {
    // TODO: display total size of code segments between load module and routine map size
    if (routineMap.empty()) return;
    const Size 
        visitedCount = visitedNames.size(),
        ignoredCount = ignoredNames.size(),
        routineMapSize = routineMap.size(),
        loadModuleSize = size();
    Size routineSumSize = 0, reachableSize = 0, unreachableSize = 0, 
        excludedSize = 0, excludedCount = 0, excludedReachableSize = 0, missedSize = 0, ignoredSize = 0;
    vector<std::string> missedNames;
    for (Size i = 0; i < routineMapSize; i++) {
        // gather static routine map stats
        const Routine r = routineMap.getRoutine(i);
        routineSumSize += r.size();
        reachableSize += r.reachableSize();
        unreachableSize += r.unreachableSize();
        if (r.ignore || (r.assembly && !ctx.options.checkAsm)) {
            excludedCount++;
            excludedSize += r.size();
            excludedReachableSize += r.reachableSize();
        }
        // check against routines which we actually visited at runtime
        // report routines in map which were not visited as missed, unless they were assembly 
        // and we are not in checkAsm mode
        else if (visitedNames.count(r.name) == 0 && (!r.assembly || ctx.options.checkAsm)) {
            missedSize += r.size();
            missedNames.push_back(r.toString(false));
        }
        // sum up actual encountered but ignored area during comparison
        if (ignoredNames.count(r.name)) {
            ignoredSize += r.size();
        }
    }
    ostringstream msg;
    msg << "Load module of executable is " << sizeStr(loadModuleSize) << " bytes"
        << endl << "--- Routine map stats (static):" << endl
        << "Routine map of " << routineMapSize << " routines covers " << sizeStr(routineSumSize) 
        << " bytes (" << ratioStr(routineSumSize,loadModuleSize) << " of the load module)"
        << endl
        << "Reachable code totals " << sizeStr(reachableSize) << " bytes (" << ratioStr(reachableSize, routineSumSize) << " of the mapped area)" 
        << endl
        << "Unreachable code totals " << sizeStr(unreachableSize) << " bytes (" << ratioStr(unreachableSize, routineSumSize) << " of the mapped area)" << endl;
    if (excludedCount) {
        msg << "Excluded " << excludedCount << " routines take " << sizeStr(excludedSize) 
            << " bytes ("  << ratioStr(excludedSize, routineSumSize) << " of the mapped area)"
            << endl
            << "Reachable area of excluded routines is " << sizeStr(excludedReachableSize) << " bytes (" << ratioStr(excludedReachableSize, reachableSize) << " of the reachable area)" << endl;
    }
    msg << "--- Comparison run stats (dynamic):" << endl
        << "Seen " << to_string(visitedCount) << " routines, visited " << visitedCount - ignoredCount
        << " and compared " << sizeStr(comparedSize) << " bytes of opcodes inside (" << ratioStr(comparedSize, reachableSize) << " of the reachable area)"
        << endl
        << "Ignored (seen but excluded) " << ignoredCount << " routines totaling " << sizeStr(ignoredSize) << " bytes (" << ratioStr(ignoredSize, reachableSize) << " of the reachable area)"
        << endl
        << "Practical coverage (visited & compared + ignored) is " << sizeStr(comparedSize + ignoredSize)
        << " (" << output_color(OUT_GREEN) << ratioStr(comparedSize + ignoredSize, reachableSize) << output_color(OUT_DEFAULT) << " of the reachable area)"
        << endl
        << "Theoretical(*) coverage (visited & compared + reachable excluded area) is " << sizeStr(comparedSize + excludedReachableSize) 
        << " (" << output_color(OUT_GREEN) << ratioStr(comparedSize + excludedReachableSize, reachableSize) << output_color(OUT_DEFAULT) << " of the reachable area)";
    if (missedNames.size()) {
        msg << endl
            << "Missed (not seen and not excluded) " << missedNames.size() 
            << " routines totaling " << sizeStr(missedSize) 
            << " bytes (" << output_color(OUT_RED) << ratioStr(missedSize, routineSumSize) << output_color(OUT_DEFAULT) << " of the covered area)";
        if (showMissed) for (const auto &n : missedNames) msg << endl << n;
    }
    msg << endl 
        << "(*) Any routines called only by ignored routines have not been seen and will lower the practical score," << endl 
        << "    but theoretically if we stepped into ignored routines, we would have seen and ignored any that were excluded.";
    verbose(msg.str());
}
