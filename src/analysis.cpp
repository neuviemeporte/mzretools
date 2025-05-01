#include "dos/analysis.h"
#include "dos/opcodes.h"
#include "dos/output.h"
#include "dos/util.h"
#include "dos/error.h"
#include "dos/executable.h"
#include "dos/editdistance.h"

#include <iostream>
#include <istream>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <string>
#include <cstring>
#include <map>

using namespace std;

#ifdef DEBUG
#define DUMP_REGS(regs) debug(regs.toString())
#else
#define DUMP_REGS(regs)
#endif

OUTPUT_CONF(LOG_ANALYSIS)

void searchMessage(const Address &addr, const string &msg) {
    output(addr.toString() + ": " + msg, LOG_ANALYSIS, LOG_DEBUG);
}

bool OffsetMap::codeMatch(const Address from, const Address to) {
    if (!(from.isValid() && to.isValid()))
        return false;

    // mapping already exists
    if (codeMap.count(from) > 0) {
        auto &found = codeMap[from];
        if (found == to) {
            debug("Existing code address mapping " + from.toString() + "->" + to.toString() + " matches");
            return true;
        }
        error("Code address mapping " + from.toString() + "->" + to.toString() + " collides with existing " + from.toString() + "->" + found.toString());
        return false;
    }
    // ensure uniqueness in other direction (no duplicate "to"s across all mappings)
    for (const auto& [f, t] : codeMap) if (t == to) {
        error("Code address mapping " + from.toString() + "->" + to.toString() + " collides with existing " + f.toString() + "<-" + t.toString());
        return false;
    }
    // otherwise save new mapping
    debug("Registering new code address mapping: " + from.toString() + "->" + to.toString());
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
        // ensure uniqueness in other direction (duplicate "to"s across all mappings don't exceed the max data segment count)
        // TODO: come up with something better for multiple data segments than just Nx possible matches, these still could be wrong between segments.
        // Essentially we need to know the current data segment for every comparison location, put entire ds:off addresses into the map and compare accordingly,
        // perhaps store this information from the mzmap run?
        Size toCount = 0;
        for (const auto& [f, tv] : dataMap) {
            if (std::find(begin(tv), end(tv), to) != tv.end()) {
                toCount++;
                if (toCount < maxData) {
                    debug("Data offset mapping " + hexVal(from) + "->" + hexVal(to) + " colides with existing " + hexVal(f) + "->" + dataStr(tv) 
                        + ", allowed " + to_string(toCount) + "/" + to_string(maxData));
                }
                else {
                    error("Data offset mapping " + hexVal(from) + "->" + hexVal(to) + " colides with existing " + hexVal(f) + "->" + dataStr(tv));
                    return false;
                }
            }
        }
        debug("Registering new data offset mapping: " + hexVal(from) + " -> " + hexVal(to));
        mappings.push_back(to);
        return true;
    }
    // no matching mapping and limit already reached
    else {
        error("Data offset mapping " + hexVal(from) + "->" + hexVal(to) + " colides with existing " + hexVal(from) + "->" + dataStr(mappings));
        return false;
    }
}

bool OffsetMap::stackMatch(const SOffset from, const SOffset to) {
    // check if mapping already exists
    if (stackMap.count(from) > 0) {
        const SOffset existing = stackMap[from];
        if (existing == to) {
            debug("Existing stack offset mapping " + hexVal(from) + "->" + hexVal(to) + " matches");
            return true;
        }
        error("Stack offset mapping " + hexVal(from) + "->" + hexVal(to) + " collides with existing: " + hexVal(from) + "->" + hexVal(existing));
        return false;
    }
    // ensure uniqueness in other direction (no duplicate "to"s across all mappings)
    for (const auto& [f,t] : stackMap) if (t == to) {
        error("Stack offset mapping " + hexVal(from) + "->" + hexVal(to) + " collides with existing: " + hexVal(f) + "->" + hexVal(t));
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

static void applyMov(const Instruction &i, CpuState &regs, Executable &exe) {
    // we only care about mov-s into registers
    // TODO: support mov mem, reg? would need to keep stack of memories per queue item... maybe just a delta list to keep storage down?    
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
            Byte byteVal;
            Word wordVal;
            switch(i.op2.size) {
            case OPRSZ_BYTE:
                byteVal = code.readByte(srcAddr);
                searchMessage(i.addr, "source address for byte: " + srcAddr.toString() + ", value in memory: " + hexVal(byteVal));
                regs.setValue(dest, byteVal); 
                set = true;
                break;
            case OPRSZ_WORD:
                wordVal = code.readWord(srcAddr);
                searchMessage(i.addr, "source address for word: " + srcAddr.toString() + ", value in memory: " + hexVal(wordVal));
                regs.setValue(dest, wordVal);
                set = true;
                break;
            }
        }
        else searchMessage(i.addr, "mov source address outside code extents: "s + srcAddr.toString());
    }
    // create data/stack segments from mov ds/es/ss stores
    if (set) {
        searchMessage(i.addr, "executed move to register: "s + i.toString());
        switch (i.op1.type) {
        case OPR_REG_DS: 
            exe.storeSegment({"", Segment::SEG_DATA, regs.getValue(REG_DS)});
            break;
        case OPR_REG_ES: 
            exe.storeSegment({"", Segment::SEG_DATA, regs.getValue(REG_ES)});
            break;
        case OPR_REG_SS:
            exe.storeSegment({"", Segment::SEG_STACK, regs.getValue(REG_SS)});
            break;
        }
    }
    DUMP_REGS(regs);
}

// TODO: all this needs to get properly implemented as full cpu instructions instead of these lame approximations
static void applyPush(const Instruction &i, CpuState &regs) {
    if (i.iclass != INS_PUSH) throw AnalysisError("Attempted to apply invalid push instruction");
    const Register reg = i.op1.regId();
    if (reg == REG_NONE) {
        // TODO: could try supporting pushes from mem, not sure if it would really improve discovery
        debug("Ignoring non-register push, wiping simulated stack");
        regs.clearStack();
        return;
    }
    if (!regs.isKnown(reg)) {
        debug("Attempted push of unknown value, ignoring and wiping simulated stack");
        regs.clearStack();
        return;
    }
    const Word pushVal = regs.getValue(reg);
    debug("Pushing value of " + hexVal(pushVal));
    regs.push(pushVal);
    DUMP_REGS(regs);
}

static void applyPop(const Instruction &i, CpuState &regs, Executable &exe) {
    if (i.iclass != INS_POP) throw AnalysisError("Attempted to apply invalid pop instruction");
    const Register reg = i.op1.regId();
    if (regs.stackEmpty()) {
        debug("Attempted to pop from empty stack, ignoring");
        return;
    }
    const Word popVal = regs.pop();
    debug("Popping value of " + hexVal(popVal));
    if (reg != REG_NONE) {
        regs.setValue(reg, popVal);
        DUMP_REGS(regs);
        switch (reg) {
        case REG_DS: 
        case REG_ES: 
            exe.storeSegment({"", Segment::SEG_DATA, popVal});
            break;
        case REG_SS: 
            exe.storeSegment({"", Segment::SEG_STACK, popVal});
            break;
        }
    }
}

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

// for executables whose layout is known in advance (but we still want to determine the routine boundaries), like when we built it ourselves
// and have the linker map, seed the scan queue for code exploration with all known routine entrypoint locations
void Analyzer::seedQueue(const CodeMap &map, Executable &exe) {
    debug("Seeding scan queue from code map");
    CpuState initRegs{exe.entrypoint(), exe.stackAddr()};
    scanQueue = ScanQueue{exe.loadAddr(), exe.size(), {}};
    // seed segments
    exe.clearSegments();
    debug("Seeding with " + to_string(map.segmentCount()) + " segments");
    for (const Segment &seg : map.getSegments()) {
        debug("Attempting to seed segment: " + seg.toString());
        exe.storeSegment(seg);
    }
    // try to add the entrypoint segment explicitly in case it was missed
    exe.storeSegment(Segment{"", Segment::SEG_CODE, exe.entrypoint().segment});
    // seed routines
    debug("Seeding with " + to_string(map.routineCount()) + " entrypoints");
    for (Size ri = 0; ri < map.routineCount(); ++ri) {
        const Routine r = map.getRoutine(ri);
        debug("Attempting to seed routine: " + r.toString());
        scanQueue.saveCall(r.entrypoint(), initRegs, false, r.name);
    }
    // try to add the executable entrypoint explicitly in case it was missed
    scanQueue.saveCall(exe.entrypoint(), initRegs, true, "start");
    // seed vars
    debug("Seeding with " + to_string(map.variableCount()) + " variables");
    for (Size vi = 0; vi < map.variableCount(); ++vi) {
        const Variable v = map.getVariable(vi);
        debug("Seeding variable: " + v.toString());
        vars.insert(v);
    }
    debug("Scan queue seeding complete");
}

// check for one or more nops past current instruction, mark as belonging to this routine if detected
void Analyzer::claimNops(const Instruction &i, const Executable &exe) {
    Address 
        curAddr = i.addr,
        nextAddr{curAddr + static_cast<SByte>(i.length)};
    try {
        while (nextAddr > curAddr && exe.extents().contains(nextAddr) && Instruction{nextAddr, exe.codePointer(nextAddr)}.iclass == INS_NOP) {
            scanQueue.setRoutineIdx(nextAddr.toLinear(), 1);
            curAddr = nextAddr;
            nextAddr++;
        }
    }
    catch (CpuError &e) {
        // encountered invalid instruction while trying to peek ahead, silently ignore
        return;
    }
}

// explore the code without actually executing instructions, discover routine boundaries
// TODO: identify routines through signatures generated from OMF libraries
// TODO: trace usage of bp register (sub/add) to determine stack frame size of routines
// TODO: store references to potential jump tables (e.g. jmp cs:[bx+0xc08]), if unclaimed after initial search, try treating entries as pointers and run second search before coalescing blocks?
// TODO: this is single-shot, need to wipe object state before done so that same analyzer can be used again
CodeMap Analyzer::exploreCode(Executable &exe) {
    CpuState initRegs{exe.entrypoint(), exe.stackAddr()};
    
    debug("initial register values:\n"s + initRegs.toString());
    // initialize queue for BFS search only if it's not been seeded already
    if (scanQueue.empty()) scanQueue = ScanQueue{exe.loadAddr(), exe.size(), Destination(exe.entrypoint(), 1, true, initRegs)};
    info("Analyzing code within extents: "s + exe.extents());
    Size locations = 0;
 
    // iterate over entries in the search queue
    while (!scanQueue.empty()) {
        // get a location from the queue and jump to it
        const Destination search = scanQueue.nextPoint();
        const RoutineEntrypoint ep = scanQueue.getEntrypoint(search.routineIdx);
        locations++;
        Address csip = search.address;
        searchMessage(csip, "--- Scanning at new location from routine " + ep.toString() + ", call: "s + to_string(search.isCall) + ", queue = " + to_string(scanQueue.size()));
        CpuState regs = search.regs;
        regs.setValue(REG_CS, csip.segment);
        DUMP_REGS(regs);
        exe.storeSegment({"", Segment::SEG_CODE, csip.segment});
        try {
            // iterate over instructions at current search location in a linear fashion, until an unconditional jump or return is encountered
            while (true) {
                if (!exe.contains(csip))
                    throw AnalysisError("Advanced past loaded code extents: "s + csip.toString());
                // check if this location was visited before
                const auto rid = scanQueue.getRoutineIdx(csip.toLinear());
                if (rid == BAD_ROUTINE) {
                    searchMessage(csip, "Location previously marked as bad, ignoring");
                    break;
                }
                // make sure we do not steamroll over previously explored instructions from a wild jump (a call has precedence)
                if (rid != NULL_ROUTINE && !search.isCall) {
                    searchMessage(csip, "Location already claimed by routine "s + to_string(rid) + " and search point did not originate from a call, halting scan");
                    break;
                }
                const auto atEntrypoint = scanQueue.isEntrypoint(csip);
                // similarly, protect yet univisited locations which are however recognized as routine entrypoints, unless visiting from a matching routine id
                if (atEntrypoint != NULL_ROUTINE && atEntrypoint != search.routineIdx) {
                    searchMessage(csip, "Location marked as entrypoint for routine "s + to_string(atEntrypoint) + " while scanning from " + to_string(search.routineIdx) + ", halting scan");
                    break;
                }
                Instruction i(csip, exe.codePointer(csip));
                regs.setValue(REG_IP, csip.offset);
                // mark memory map items corresponding to the current instruction as belonging to the current routine 
                // (routine id is tracked by the queue, no need to provide)
                scanQueue.setRoutineIdx(csip.toLinear(), i.length);
                // if the instruction references memory, save the location as a potential data item
                processDataReference(exe, i, regs);
                // interpret the instruction
                if (i.isBranch()) {
                    const Branch branch = getBranch(exe, i, regs);
                    debug("Encountered branch: " + branch.toString());
                    // if the destination of the branch can be established, place it in the search queue
                    scanQueue.saveBranch(branch, regs, exe.extents());
                    // for a call or conditional branch, we can continue scanning (fall-through), but do it under a new search queue location 
                    // to have finer granularity in case we run into data in the middle of code and have to rollback the whole block as bad
                    if (branch.isCall || branch.isConditional) {
                        Branch ftBranch;
                        ftBranch.destination = csip + static_cast<Offset>(i.length);
                        const Offset destOffset = ftBranch.destination.toLinear();
                        const RoutineIdx destId = scanQueue.getRoutineIdx(destOffset);
                        if (branch.isCall) {
                            // pessimistically, every register can be modified after returning from a call, except for CS, but let's be generous with DS and SS, otherwise we break too much stuff
                            assert(regs.isKnown(REG_CS));
                            const bool
                                knownDS = regs.isKnown(REG_DS),
                                knownSS = regs.isKnown(REG_SS);
                            const Word 
                                cs = regs.getValue(REG_CS),
                                ds = regs.getValue(REG_DS),
                                ss = regs.getValue(REG_SS);
                            regs.reset();
                            regs.setValue(REG_CS, cs);
                            if (knownDS) regs.setValue(REG_DS, ds);
                            if (knownSS) regs.setValue(REG_SS, ss);
                        }
                        if (destId != search.routineIdx) {
                            ftBranch.isCall = false;
                            ftBranch.isNear = true;
                            ftBranch.isConditional = false;
                            debug("Saving fall-through branch: " + ftBranch.toString() + " over id " + to_string(destId));
                            // make the destination of this fake "branch" undiscovered in the queue, wipe any claiming routine id run
                            scanQueue.clearRoutineIdx(destOffset);
                            scanQueue.saveBranch(ftBranch, regs, exe.extents());
                        }
                        break;
                    }
                    // if the branch is an unconditional jump, we cannot keep scanning past it, regardless of the destination resolution
                    else {
                        searchMessage(csip, "routine scan interrupted by unconditional branch");
                        claimNops(i, exe);
                        break;
                    }
                }
                else if (i.isReturn()) {
                    // TODO: check if return matches entrypoint type (near/far), warn otherwise
                    searchMessage(csip, "routine scan interrupted by return");
                    //claimNops(i, exe);
                    break;
                }
                // limited register value tracing to enable data/stack segment deduction
                else if (i.iclass == INS_MOV) applyMov(i, regs, exe);
                else if (i.iclass == INS_PUSH) applyPush(i, regs);
                else if (i.iclass == INS_POP) applyPop(i, regs, exe);
                // interrupts which don't return
                else if ((i.isInt(0x21) && regs.getValue(REG_AH) == 0x4c) // 21.4c: exit with code
                        || (i.isInt(0x21) && regs.getValue(REG_AH) == 0x31) // 21.31: dos 2+ tsr
                        || (i.isInt(0x21) && regs.getValue(REG_AH) == 0x0) // 21.0: terminate
                        || (i.isInt(0x27) && regs.getValue(REG_DX) == 0xfff0) // 27,dx=fff0: dos 1+ tsr
                        || (i.isInt(0x20))) { // 20: terminate
                    searchMessage(csip, "routine scan interrupted by non-returning interrupt");
                    break;
                }
                else {
                    for (Register r : i.touchedRegs()) {
                        regs.setUnknown(r);
                    }
                }
                // advance to next instruction
                csip += i.length;
            } // next instruction at current search location
        } // try block
        catch (CpuError &e) {
            debug("Routine discovery encountered CPU exception at " + csip.toString() + ": " + e.why());
            const Address start = search.address;
            assert(start <= csip);
            const Size rollbackSize = csip.toLinear() - start.toLinear() + 1;
            debug("Search started at " + start.toString() + ", rolling back " + sizeStr(rollbackSize) + " bytes, marking bad");
            scanQueue.setRoutineIdx(start.toLinear(), rollbackSize, BAD_ROUTINE);
        }
    } // next search location from search queue
    info("Done analyzing code, examined " + to_string(locations) + " locations");
#ifdef DEBUG
    scanQueue.dumpVisited("routines.visited");
#endif

    // create routine map from contents of search queue
    auto ret = CodeMap{scanQueue, exe.getSegments(), vars, exe.getLoadSegment(), exe.size()};
    // TODO: stats like for compare
    return ret;
}

void Analyzer::checkMissedRoutines(const CodeMap &refMap) {
    // update set of missed routines
    calculateStats(refMap);
    const Size missedCount = missedNames.size();
    if (missedCount == 0) {
        debug("No missed routines detected");
        return;
    }
    verbose("Adding " + to_string(missedCount) + " missed routines to queue");
    // go over missed routines, manually insert entrypoints into comparison location queue
    for (const auto &rn : missedNames) {
        const Routine mr = refMap.getRoutine(rn);
        if (!mr.isValid()) throw LogicError("Unable to find missed routine " + rn + " in routine map");
        debug("Inserting routine " + mr.name + " into queue, entrypoint: " + mr.entrypoint().toString());
        scanQueue.saveCall(mr.entrypoint(), {}, mr.near);
    }
}

Address Analyzer::findTargetLocation(const Executable &ref, const Executable &tgt) {
    Address ret;
    Address seqEnd = refCsip;
    std::vector<Address> matchLocations;
    ByteString searchString;

    if (!compareBlock.isValid()) {
        error("Current comparison block is invalid");
        return ret;
    }
    const auto unvisited = tgtQueue.getUnvisited();
    if (unvisited.empty()) {
        error("Target executable contains no unvisited blocks");
        return ret;
    }

    debug("Trying to find equivalent target location of reference address " + refCsip.toString() + " across " + to_string(unvisited.size()) + " unvisited target blocks");
    while (true) {
        // get next instruction, extract its pattern
        Instruction curInstr{seqEnd, ref.codePointer(seqEnd)};
        if (!compareBlock.contains(seqEnd + static_cast<Offset>(curInstr.length - 1))) {
            error("Instruction at " + seqEnd.toString() + " exceeds bounds of current reference routine block: " + compareBlock.toString());
            break;
        }
        const auto curPattern = curInstr.pattern();
        // append current instruction's pattern to the search string
        std::move(curPattern.begin(), curPattern.end(), std::back_inserter(searchString));
        debug("Current instruction at " + seqEnd.toString() + ": " + curInstr.toString() + ", search pattern now: " + numericToHexa(searchString));
        // try to find the search string across all unvisited blocks of target executable
        matchLocations.clear();
        for (const auto &u : unvisited) {
            debug("Searching in block " + u.toString());
            const auto found = tgt.find(searchString, u);
            if (found.isValid()) {
                debug("Found pattern at " + found.toString());
                matchLocations.push_back(found);
            }
        }
        const auto matchCount = matchLocations.size();
        if (matchCount == 0) {
            // TODO: if there are addresses from the previous iteration, return multiple, but would need support for multiple destinations in Analyzer
            error("Unable to find a match for location " + refCsip.toString() + " in target executable");
            break;
        }
        else if (matchCount == 1) {
            debug("Found single match, done with target search");
            break;
        }
        // more than one match found, need to disambiguate by expanding the search string in the next iteration
        // advance address of search sequence end
        seqEnd += curInstr.length;
        if (!compareBlock.contains(seqEnd)) {
            warn("Exceeded current reference routine block with " + to_string(matchCount) + " candidates, terminating search before " + seqEnd.toString());
            break;
        }
        debug("Found " + to_string(matchCount) + " matches, extending search string by next instruction at " + seqEnd.toString());
    }

    // TODO: return multi
    if (!matchLocations.empty()) ret = matchLocations.front();
    return ret;
}

// TODO: implement register value tracing like in exploreCode
bool Analyzer::compareCode(const Executable &ref, Executable &tgt, const CodeMap &refMap) {
    verbose("Comparing code between reference (entrypoint "s + ref.entrypoint().toString() + ") and target (entrypoint " + tgt.entrypoint().toString() + ") executables");
    debug("Routine map of reference binary has " + to_string(refMap.routineCount()) + " entries");
    // find name of reference entrypoint routine for seeding queues
    const Routine epr = refMap.getRoutine(ref.entrypoint());
    string eprName;
    if (epr.isValid()) {
        eprName = epr.name;
        debug("Found entrypoint routine: " + eprName);
    }
    offMap = OffsetMap{refMap.segmentCount(Segment::SEG_DATA)};
    scanQueue = ScanQueue{ref.loadAddr(), ref.size(), Destination(ref.entrypoint(), VISITED_ID, true, {}), eprName};
    tgtQueue = ScanQueue{tgt.loadAddr(), tgt.size(), Destination(tgt.entrypoint(), VISITED_ID, true, {}), eprName};
    // map of equivalent addresses in the compared binaries, seed with the two entrypoints
    offMap.setCode(ref.entrypoint(), tgt.entrypoint());
    routineNames.clear();
    excludedNames.clear();
    bool success = true;
    // iterate over queue of comparison locations
    while (!scanQueue.empty()) {
        // get next location for linear scan and comparison of instructions from the front of the queue,
        // to visit functions in the same order in which they were first encountered
        const Destination compare = scanQueue.nextPoint();
        verbose("New comparison location "s + compare.address.toString() + ", queue size = " + to_string(scanQueue.size()));
        // when entering a routine, forget all the current stack offset mappings
        if (compare.isCall) {
            offMap.resetStack();
        }
        refCsip = compare.address;
        if (options.stopAddr.isValid() && refCsip >= options.stopAddr) {
            verbose("Reached stop address: " + refCsip.toString());
            break;
        }        
        if (scanQueue.getRoutineIdx(refCsip.toLinear()) != NULL_ROUTINE) {
            debug("Location already compared, skipping");
            continue;
        }
        routine = {"unknown", {}};
        tgtCsip = offMap.getCode(refCsip);
        Size routineCount = 0;
        if (!refMap.empty()) { // comparing with a map
            // determine the reference executable routine that we are currently in
            routine = refMap.getRoutine(refCsip);
            // make sure we are inside a reachable block of a known routine from reference binary
            if (!routine.isValid()) {
                error("Could not find address "s + refCsip.toString() + " in routine map");
                success = false;
                break;
            }
            routineNames.insert(routine.name);
            compareBlock = routine.blockContaining(compare.address);
            if (!compareBlock.isValid()) {
                error("Comparison address "s + compare.address.toString() + " does not belong to any routine");
                success = false;
                break;
            }
            if (routine.ignore || (routine.assembly && !options.checkAsm)) {
                verbose("--- Skipping excluded routine " + routine.dump(false) + " @"s + refCsip.toString() + ", block " + compareBlock.toString(true) +  ", target @" + tgtCsip.toString());
                excludedNames.insert(routine.name);
                continue;
            }
            // get corresponding address for comparison in target binary
            if (!tgtCsip.isValid()) {
                // last resort, try to search by instruction opcodes if not present in offset map from observing call destinations
                tgtCsip = findTargetLocation(ref, tgt);
                if (!tgtCsip.isValid()) {
                    error("Could not find equivalent address for "s + refCsip.toString() + " in address map for target executable");
                    success = false;
                    break;
                }
                // add routine entrypoint to target queue, otherwise it will not get marked as visited when comparing
                if (refCsip == routine.entrypoint()) {
                    tgtQueue.saveCall(tgtCsip, {}, routine.near, routine.name);
                }
            }
            tgt.storeSegment({"", Segment::SEG_CODE, tgtCsip.segment});
            verbose("--- Now @"s + refCsip.toString() + ", routine " + routine.dump(false) + ", block " + compareBlock.toString(true) +  ", target @" + tgtCsip.toString());
        }
        // TODO: consider dropping this "feature"
        else { // comparing without a map
            verbose("--- Comparing reference @ "s + refCsip.toString() + " to target @" + tgtCsip.toString());
        }

        // keep comparing subsequent instructions from current search queue location between the reference and target binary
        if (!comparisonLoop(ref, tgt, refMap)) {
            success = false;
            break;
        }

        // before terminating, check for any routines missed from the reference map
        if (scanQueue.empty()) checkMissedRoutines(refMap);
    } // iterate over comparison location queue

    tgtQueue.dumpVisited("tgt.visited");
    // save target map file regardless of comparison result (can be incomplete)
    if (options.tgtMapPath.empty()) options.tgtMapPath = replaceExtension(options.mapPath, "tgt");
    if (!options.tgtMapPath.empty()) {
        debug("Constructing target map from target queue contents");
        // TODO: generate variables for target
        CodeMap tgtMap{tgtQueue, tgt.getSegments(), {}, tgt.getLoadSegment(), tgt.size()};
        //tgtMap.setSegments(tgt.getSegments());
        //tgtMap.order();
        info("Saving target map to " + options.tgtMapPath);
        tgtMap.save(options.tgtMapPath, tgt.loadAddr().segment, true);
    }

    if (success) {
        verbose(output_color(OUT_GREEN) + "Comparison result: match" + output_color(OUT_DEFAULT));
        comparisonSummary(ref, refMap, true);
    }
    else verbose(output_color(OUT_RED) + "Comparison result: mismatch" + output_color(OUT_DEFAULT));
    return success;
}

bool Analyzer::compareData(const Executable &ref, const Executable &tgt, const CodeMap &refMap, const CodeMap &tgtMap, const std::string &segment) {
    const Word 
        refLoadSeg = ref.getLoadSegment(),
        tgtLoadSeg = tgt.getLoadSegment();
    Segment refSeg = refMap.findSegment(segment);
    if (refSeg.type != Segment::SEG_DATA) throw AnalysisError("Unable to find data segment with name: " + segment + " in reference map");
    const Address refOrig{refSeg.address, 0};
    refSeg.address -= refLoadSeg;
    Segment tgtSeg = tgtMap.findSegment(segment);
    if (tgtSeg.type != Segment::SEG_DATA) throw AnalysisError("Unable to find data segment with name: " + segment + " in target map");
    const Address tgtOrig{tgtSeg.address, 0};
    tgtSeg.address -= tgtLoadSeg;
    const Address refAddr{refSeg.address, 0}, tgtAddr{tgtSeg.address, 0};
    // make sure start within executable bounds
    if (refAddr.toLinear() >= ref.size()) 
        throw AnalysisError("Reference segment " + segment + " at " + refAddr.toString() + " exceeds executable size " + sizeStr(ref.size()));
    if (tgtAddr.toLinear() >= tgt.size()) 
        throw AnalysisError("Target segment " + segment + " at " + tgtAddr.toString() + " exceeds executable size " + sizeStr(tgt.size()));        
    verbose("Found data segment " + segment + ": reference " + refAddr.toString() + ", relocated " + refOrig.toString() + ", target: " + tgtAddr.toString() + ", relocated " + tgtOrig.toString());
    Address refEnd = refAddr;
    // find segment past the segment we are looking at, if any
    for (Segment s : refMap.getSegments()) {
        s.address -= ref.getLoadSegment();
        if (s.address > refEnd.segment && SEG_TO_OFFSET(s.address) < ref.size()) refEnd = Address{s.address, 0};
    }
    // use the executable size as the end address if no segment past the examined one exists
    if (refEnd != refAddr) 
        debug("Found reference end address from next segment: " + refEnd.toString());
    else { // the examined segment is the last known one in the executable 
        const Size s = ref.size() - refAddr.toLinear() - 1;
        if (s < SEGMENT_SIZE) refEnd += s;
        // the examined segment occupies exactly 64k so move the end address to the beginning of the next segment
        else refEnd = { static_cast<Word>(refEnd.segment + 0x1000), 0 };
        debug("Set reference end address from executable size: " + refEnd.toString()); 
    }
    // make sure reference end within executable bounds
    if (refEnd.toLinear() >= ref.size()) 
        throw AnalysisError("Reference end address at " + refEnd.toString() + " exceeds executable size " + sizeStr(ref.size()));
    // infer and check target end address
    // TODO: calculcate target end too, warn on length mismatch?
    Size compareSize = refEnd - refAddr;
    verbose("Calculated size of compared segment: " + sizeStr(compareSize));
    if (compareSize > SEGMENT_SIZE) throw AnalysisError("Compared segment size more than 64k");
    Address tgtEnd{tgtSeg.address, refEnd.offset};
    if (tgtEnd.toLinear() >= tgt.size()) {
        const Size excess = tgtEnd.toLinear() - tgt.size();
        warn("Target end address at " + tgtEnd.toString() + " exceeds executable size " + sizeStr(tgt.size()) + " by " + sizeStr(excess) + ", limiting range");
        compareSize -= excess;
        refEnd.offset -= excess;
        tgtEnd.offset -= excess;
    }
    verbose("Ending comparison at " + refEnd.toString() + " / " + tgtEnd.toString()); 
    // grab pointers to beginning of data segment bytes
    const Byte 
        *refData = ref.codePointer(refOrig),
        *tgtData = tgt.codePointer(tgtOrig);
    // comparison loop
    for (Size i = 0; i < compareSize; ++i) {
        //debug(hexVal(refData[i]) + " == " + hexVal(tgtData[i]));
        if (refData[i] == tgtData[i]) continue;
        // difference found
        Address refMismatch = refAddr + i, tgtMismatch = tgtAddr + i;
        verbose("Mismatch at location " + output_color(OUT_RED) + refMismatch.toString() + " / " + tgtMismatch.toString() + output_color(OUT_DEFAULT) +  ", " + ratioStr(i, compareSize) + " into the segment");
        // find the variables around the mismatch location if possible
        Variable before, after;
        for (Size vi = 0; vi < refMap.variableCount(); ++vi) {
            Variable v = refMap.getVariable(vi);
            v.addr.rebase(refLoadSeg);
            //debug("Var " + v.toString() + " vs mismatch " + refMismatch.toString());
            if (v.addr.segment != refMismatch.segment) continue;
            if (!before.addr.isValid()) before = v;
            else if (v.addr < refMismatch) before = v;
            else if (v.addr == refMismatch) { before = after = v; break; }
            else { after = v; break; }
        }
        // show variable info if available
        if (before.addr.isValid() && after.addr.isValid()) {
            if (before.addr == after.addr) verbose("Mismatch on variable " + output_color(OUT_BLUE) + before.toString() + output_color(OUT_DEFAULT));
            else verbose("Mismatch between variables " + output_color(OUT_BLUE) + before.toString() + output_color(OUT_DEFAULT) + " and " + output_color(OUT_BLUE) + after.toString() + output_color(OUT_DEFAULT));
        }
        else verbose("No variable information around mismatch");
        // hex diff
        if (getOutputLevel() <= LOG_VERBOSE) {
            Size
                hexDumpLength = options.dataCtxCount,
                hexStart = i > (hexDumpLength / 2) ? i - (hexDumpLength / 2) : 0,
                hexEnd = i + (hexDumpLength / 2) < compareSize ? i + (hexDumpLength/2) : compareSize - 1;
            if ((hexDumpLength & 1) == 0) hexEnd--;
            debug("Starting hex diff at offset " + hexVal(hexStart) + ", ending at " + hexVal(hexEnd) + ", length = " + sizeStr(hexDumpLength) + ", compareSize = " + hexVal(compareSize) + ", i = " + hexVal(i));
            hexDiff(refData, tgtData, hexStart, hexEnd, refSeg.address, tgtSeg.address);
        }
        if (!options.extData && before.addr.isValid() && before.external) {
            verbose("Variable " + before.toString() + " marked external, ignoring mismatch");
            continue;
        }        
        verbose("Comparison result: mismatch", OUT_RED);
        return false;
    }
    verbose("Comparison result: match", OUT_GREEN);
    return true;
}

bool Analyzer::skipAllowed(const Instruction &refInstr, Instruction tgtInstr) {
    // first try skipping in the reference executable, skipping returns is not allowed
    if (refSkipCount + 1 <= options.refSkip && !refInstr.isReturn()) {
        // save original reference position for rewind
        if (refSkipCount == 0) refSkipOrigin = refCsip;
        skipType = SKIP_REF;
        refSkipCount++;
        return true;
    }
    // no more skips allowed in reference, try skipping target executable
    else if (tgtSkipCount + 1 <= options.tgtSkip && !tgtInstr.isReturn()) {
        // save original target position for skip context display
        if (tgtSkipCount == 0) tgtSkipOrigin = tgtCsip;
        skipType = SKIP_TGT;
        // performing a skip in the target executable resets the allowed reference skip count
        refSkipCount = 0;
        tgtSkipCount++;
        return true;
    }
    // ran out of allowed skips in both executables
    return false;
}

bool Analyzer::compareInstructions(const Executable &ref, const Executable &tgt, const Instruction &refInstr, Instruction tgtInstr) {
    skipType = SKIP_NONE;
    matchType = instructionsMatch(ref, tgt, refInstr, tgtInstr);
    switch (matchType) {
    case CMP_MATCH:
        // display skipped instructions if there were any before this match
        if (refSkipCount || tgtSkipCount) {
            skipContext(ref, tgt);
            // an instruction match resets the allowed skip counters
            refSkipCount = tgtSkipCount = 0;
            refSkipOrigin = tgtSkipOrigin = Address();
        }
        verbose(compareStatus(refInstr, tgtInstr, true));
        break;
    case CMP_MISMATCH:
        // attempt to skip a mismatch, if permitted by the options
        if (!skipAllowed(refInstr, tgtInstr)) {
            // display skipped instructions if there were any before this mismatch
            if (refSkipCount || tgtSkipCount) {
                skipContext(ref, tgt);
            }
            verbose(output_color(OUT_RED) + compareStatus(refInstr, tgtInstr, true) + output_color(OUT_DEFAULT));
            error("Instruction mismatch in routine " + routine.name + " at " + compareStatus(refInstr, tgtInstr, false));
            diffContext(ref, tgt);
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
    return true;
}

void Analyzer::advanceComparison(const Instruction &refInstr, Instruction tgtInstr) {
    switch (matchType) {
    case CMP_MISMATCH:
        // if the instructions did not match and we still got here, that means we are in difference skipping mode, 
        debug(compareStatus(refInstr, tgtInstr, false, INS_MATCH_MISMATCH));
        switch (skipType) {
        case SKIP_REF: 
            comparedSize += refInstr.length;
            refCsip += refInstr.length;
            debug("Skipping over reference instruction mismatch, allowed " + to_string(refSkipCount) + " out of " + to_string(options.refSkip) + ", destination " + refCsip.toString());
            break;
        case SKIP_TGT:
            // rewind reference position if it was skipped before
            if (refSkipOrigin.isValid()) {
                comparedSize -= refCsip.offset - refSkipOrigin.offset;
                refCsip = refSkipOrigin;
            }
            tgtCsip += tgtInstr.length;
            debug("Skipping over target instruction mismatch, allowed " + to_string(tgtSkipCount)  + " out of " + to_string(options.tgtSkip) + ", destination " + tgtCsip.toString());
            break;
        default:
            throw LogicError("Unexpected: no skip despite mismatch");
        }
        break;
    case CMP_VARIANT:
        comparedSize += refInstr.length;
        refCsip += refInstr.length;
        // in case of a variant match, the instruction pointer in the target binary will have already been advanced by instructionsMatch()
        debug("Variant match detected, comparison will continue at " + tgtCsip.toString());
        break;
    default:
        // normal case, advance both reference and target positions
        comparedSize += refInstr.length;
        refCsip += refInstr.length;
        tgtCsip += tgtInstr.length;
        break;
    }
}

bool Analyzer::checkComparisonStop() {
    // an end of a routine block is a hard stop, we do not want to go into unreachable areas which may contain invalid opcodes (e.g. data mixed with code)
    // TODO: need to handle case where we are skipping right now
    if (compareBlock.isValid() && refCsip > compareBlock.end) {
        verbose("Reached end of routine block @ "s + compareBlock.end.toString());
        // if the current routine still contains reachable blocks after the current location, add the start of the next one to the back of the queue,
        // so it gets picked up immediately on the next iteration of the outer loop
        const Block rb = routine.nextReachable(refCsip);
        if (rb.isValid()) {
            verbose("Routine still contains reachable blocks, next @ " + rb.toString());
            scanQueue.saveJump(rb.begin, {});
            if (!offMap.getCode(rb.begin).isValid()) {
                // the offset map between the reference and the target does not have a matching entry for the next reachable block's destination,
                // so the comparison would fail - last ditch attempt is to try and record a "guess" offset mapping before jumping there,
                // based on the hope that the size of the unreachable block matches in the target
                // TODO: make sure an instruction matches at the destination before actually recording the mapping
                const Address guess = tgtCsip + (rb.begin - refCsip);
                debug("Recording guess offset mapping based on unreachable block size: " + rb.begin.toString() + " -> " + guess.toString());
                offMap.setCode(rb.begin, guess);
            }
        }
        else {
            verbose("Completed comparison of routine " + routine.name + ", no more reachable blocks");
        }
        return true;
    }

    // reached predefined stop address
    if (options.stopAddr.isValid() && refCsip >= options.stopAddr) {
        verbose("Reached stop address: " + refCsip.toString());
        return true;
    }
    return false;
}

// compare instructions between two executables over a contiguous block
bool Analyzer::comparisonLoop(const Executable &ref, Executable &tgt, const CodeMap &refMap) {
    refSkipCount = tgtSkipCount = 0;
    const RoutineEntrypoint tgtEp = tgtQueue.getEntrypoint(routine.name);
    if (!tgtEp.addr.isValid()) {
        warn("Unable to find target entrypoint for routine " + routine.name);
        tgtQueue.dumpEntrypoints();
    }
    while (true) {
        // if we ran outside of the code extents, consider the comparison successful
        if (!ref.contains(refCsip) || !tgt.contains(tgtCsip)) {
            debug("Advanced past code extents: csip = "s + refCsip.toString() + " / " + tgtCsip.toString() + " vs extents " + ref.extents().toString() 
                + " / " + tgt.extents().toString());
            // make sure we are not skipping instructions
            // TODO: make this non-fatal, just make the skip fail
            if (refSkipCount || tgtSkipCount) return false;
            else break;
        }

        // decode instructions
        Instruction 
            refInstr{refCsip, ref.codePointer(refCsip)}, 
            tgtInstr{tgtCsip, tgt.codePointer(tgtCsip)};
        
        // mark this instruction as visited
        scanQueue.setRoutineIdx(refCsip.toLinear(), refInstr.length, VISITED_ID);
        tgtQueue.setRoutineIdx(tgtCsip.toLinear(), tgtInstr.length, tgtEp.idx);

        // compare instructions
        if (!compareInstructions(ref, tgt, refInstr, tgtInstr)) {
            if (!options.noStats) comparisonSummary(ref, refMap, false);
            return false;
        }

        // comparison result okay (instructions match or skip permitted), interpret the instructions
        // instruction is a call, save destination to the comparison queue 
        if (!options.noCall && refInstr.isCall() && tgtInstr.isCall()) {
            const Branch 
                refBranch = getBranch(ref, refInstr, {}),
                tgtBranch = getBranch(tgt, tgtInstr, {});
            // if the destination of the branch can be established, place it in the compare queue
            if (scanQueue.saveBranch(refBranch, {}, ref.extents())) {
                // if the branch destination was accepted, save the address mapping of the branch destination between the reference and target
                if (!offMap.codeMatch(refBranch.destination, tgtBranch.destination)) return false;
            }
            const Routine refRoutine = refMap.getRoutine(refBranch.destination);
            if (refRoutine.isValid()) {
                debug("Registering target call for routine " + refRoutine.name);
                tgtQueue.saveCall(tgtBranch.destination, {}, tgtInstr.isNearBranch(), refRoutine.name);
            }
        }
        // instruction is a jump, save the relationship between the reference and the target addresses into the offset map
        // TODO: isn't this already handled in instructionsMatch()?
        else if (refInstr.isJump() && tgtInstr.isJump() && skipType == SKIP_NONE) {
            const Branch 
                refBranch = getBranch(ref, refInstr, {}),
                tgtBranch = getBranch(tgt, tgtInstr, {});
            if (refBranch.destination.isValid() && tgtBranch.destination.isValid()) {
                if (!offMap.codeMatch(refBranch.destination, tgtBranch.destination)) return false;
            }
        }

        // register segment of far call with target executable
        if (refInstr.isFarCall() && !refMap.empty()) {
            const Address farDest = refInstr.op1.farAddr();
            const Word tgtSegment = tgtInstr.op1.farAddr().segment;
            auto refSegment = refMap.findSegment(farDest.segment);
            if (refSegment.type == Segment::SEG_NONE) {
                warn("Farcall segment " + hexVal(farDest.segment) + " not found in reference map");
            }
            else if (!tgt.storeSegment({"", refSegment.type, tgtSegment})) {
                warn("Unable to register farcall destination segment " + hexVal(tgtSegment) + " with target executable");
            }
        }

        // adjust position in compared executables for next iteration
        advanceComparison(refInstr, tgtInstr);

        // check loop termination conditions
        if (checkComparisonStop()) break;
    } // iterate over instructions at current comparison location

    return true;
}

Branch Analyzer::getBranch(const Executable &exe, const Instruction &i, const CpuState &regs) const {
    const Address addr = i.addr;
    Branch branch;
    branch.source = addr;
    switch (i.iclass) {
    // conditional jump
    case INS_JMP_IF:
        branch.isCall = false;
        branch.isConditional = true;
        branch.destination = i.destinationAddress();
        searchMessage(addr, "encountered conditional near jump to "s + branch.destination.toString());
        break;
    // unconditional jumps
    case INS_JMP:
        branch.isCall = false;
        branch.isConditional = false;
        switch (i.opcode) {
        case OP_JMP_Jb: 
        case OP_JMP_Jv: 
            branch.destination = i.destinationAddress();
            searchMessage(addr, "encountered unconditional near jump to "s + branch.destination.toString());
            break;
        case OP_GRP5_Ev:
            searchMessage(addr, "unknown near jump target: "s + i.toString());
            DUMP_REGS(regs);
            break; 
        default: 
            throw AnalysisError("Unsupported jump opcode: "s + hexVal(i.opcode));
        }
        break;
    case INS_JMP_FAR:
        branch.isCall = false;
        branch.isConditional = false;
        if (i.op1.type == OPR_IMM32) {
            branch.destination = Address{i.op1.immval.u32}; 
            branch.isNear = false;
            searchMessage(addr, "encountered unconditional far jump to "s + branch.destination.toString());
        }
        else {
            searchMessage(addr, "unknown far jump target: "s + i.toString());
            DUMP_REGS(regs);
        }
        break;
    // loops
    case INS_LOOP:
    case INS_LOOPNZ:
    case INS_LOOPZ:
        branch.isCall = false;
        branch.isConditional = true;
        branch.destination = i.destinationAddress();
        searchMessage(addr, "encountered loop to "s + branch.destination.toString());
        break;
    // calls
    case INS_CALL:
        branch.isCall = true;
        branch.isConditional = false;
        if (i.op1.type == OPR_IMM16) {
            branch.destination = i.destinationAddress();
            searchMessage(addr, "encountered near call to "s + branch.destination.toString());
        }
        else if (operandIsReg(i.op1.type) && regs.isKnown(i.op1.regId())) {
            branch.destination = {i.addr.segment, regs.getValue(i.op1.regId())};
            searchMessage(addr, "encountered near call through register to "s + branch.destination.toString());
        }
        else if (operandIsMemImmediate(i.op1.type) && regs.isKnown(REG_DS)) {
            // need to read call destination offset from memory
            Address memAddr{regs.getValue(REG_DS), i.op1.immval.u16};
            if (exe.contains(memAddr)) {
                branch.destination = Address{i.addr.segment, exe.getCode().readWord(memAddr)};
                searchMessage(addr, "encountered near call through mem pointer to "s + branch.destination.toString());
            }
            else debug("mem pointer of call destination outside code extents: " + memAddr.toString());
        }
        else {
            searchMessage(addr, "unknown near call target: "s + i.toString());
            DUMP_REGS(regs);
        } 
        break;
    case INS_CALL_FAR:
        branch.isCall = true;
        branch.isConditional = false;
        branch.isNear = false;
        if (i.op1.type == OPR_IMM32) {
            branch.destination = Address(DWORD_SEGMENT(i.op1.immval.u32), DWORD_OFFSET(i.op1.immval.u32));
            searchMessage(addr, "encountered far call to "s + branch.destination.toString());
        }
        else {
            searchMessage(addr, "unknown far call target: "s + i.toString());
            DUMP_REGS(regs);
        }
        break;
    default:
        throw AnalysisError("Instruction is not a branch: "s + i.toString() + ", class = " + instr_class_name(i.iclass));
    }
    return branch;
}

// dictionary of equivalent instruction sequences for variant-enabled comparison
// TODO: support user-supplied equivalence dictionary 
// TODO: do not hardcode, place in text file
// TODO: automatic reverse match generation
// TODO: replace strings with Instruction-s/Signature-s, support more flexible matching?
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

Analyzer::ComparisonResult Analyzer::variantMatch(const Executable &tgt, const Instruction &refInstr, Instruction tgtInstr) {
    bool match = false;
    // check for a variant match if allowed by options
    if (options.variant && INSTR_VARIANT.count(refInstr.toString())) {
        // get vector of allowed variants (themselves vectors of strings)
        const auto &variants = INSTR_VARIANT.at(refInstr.toString());
        debug("Found "s + to_string(variants.size()) + " variants for instruction '" + refInstr.toString() + "'");
        // compose string for showing the variant comparison instructions
        string statusStr = compareStatus(refInstr, tgtInstr, true, INS_MATCH_DIFF);
        string variantStr;
        // iterate over the possible variants of this reference instruction
        for (auto &v : variants) {
            variantStr.clear();
            match = true;
            // temporary code pointer for the target binary while we scan its instructions ahead
            Address tmpCsip = tgtCsip;
            int idx = 0;
            // iterate over instructions inside this variant
            for (auto &istr: v) {
                debug(tmpCsip.toString() + ": " + tgtInstr.toString() + " == " + istr + " ? (" + to_string(idx+1) + "/" + to_string(v.size()) + ")");
                // stringwise compare the next instruction in the variant to the current instruction
                if (tgtInstr.toString() != istr) { match = false; break; }
                // if this is not the last instruction in the variant, read the next instruction from the target binary
                tmpCsip += tgtInstr.length;
                if (++idx < v.size()) {
                    tgtInstr = Instruction{tmpCsip, tgt.codePointer(tmpCsip)};
                    variantStr += "\n" + compareStatus(Instruction(), tgtInstr, true);
                }
            }
            if (match) {
                debug("Got variant match, advancing target binary to " + tmpCsip.toString());
                verbose(output_color(OUT_YELLOW) + statusStr + variantStr + output_color(OUT_DEFAULT));
                // in the case of a match, need to update the actual instruction pointer in the target binary to account for the instructions we skipped
                tgtCsip = tmpCsip;
                return CMP_VARIANT;
            }
        }
    }
    
    return CMP_MISMATCH;
}

Analyzer::ComparisonResult Analyzer::instructionsMatch(const Executable &ref, const Executable &tgt, const Instruction &refInstr, const Instruction &tgtInstr) {
    if (options.ignoreDiff) return CMP_MATCH;

    auto insResult = refInstr.match(tgtInstr);
    // in strict mode, the instructions are expected to be exactly matching, with no variants/mapping
    if (insResult != INS_MATCH_FULL && options.strict) {
        debug("Mismatching due to difference in strict mode");
        return CMP_MISMATCH;
    }
    if (insResult == INS_MATCH_MISMATCH) {
        // special case of jmp vs jmp short - allow only if variants enabled and in assembly routines, which are not well behaved
        if (refInstr.opcode != tgtInstr.opcode && refInstr.isUnconditionalJump() && tgtInstr.isUnconditionalJump()) {
            if (options.variant || routine.assembly) {
                verbose(output_color(OUT_BRIGHTRED) + compareStatus(refInstr, tgtInstr, true, INS_MATCH_DIFF) + output_color(OUT_DEFAULT));
                tgtCsip += tgtInstr.length;
                return CMP_VARIANT;
            }
            else return CMP_MISMATCH;
        }
        // in case of complete mismatch, last ditch is to try a variant match
        return variantMatch(tgt, refInstr, tgtInstr);
    } 
    // we now either have a full match, or a difference in one or both operands, check the offsets in either case
    // (even if the instructions match fully, that could be an error if the equal offsets have previously been mapped to something else)
    bool match = false;
    if (operandIsMemWithOffset(refInstr.op1.type) || operandIsMemWithOffset(refInstr.op2.type)) {
        assert(operandIsMemWithOffset(tgtInstr.op1.type) || operandIsMemWithOffset(tgtInstr.op2.type));
        // apply mapping
        // TODO: differentiate between offs/bx+offs/bp+offs?
        SOffset refOfs = refInstr.memOffset(), tgtOfs = tgtInstr.memOffset();
        Register segReg = refInstr.memSegmentId();
        switch (segReg) {
        case REG_CS:
            match = offMap.codeMatch(refOfs, tgtOfs);
            if (!match) verbose("Instruction mismatch due to code segment offset mapping conflict");
            break;
        case REG_ES: /* TODO: come up with something better */
        case REG_DS:
            match = offMap.dataMatch(refOfs, tgtOfs);
            if (!match) verbose("Instruction mismatch due to data segment offset mapping conflict");
            break;
        case REG_SS:
            match = offMap.stackMatch(refOfs, tgtOfs);
            if (!match) verbose("Instruction mismatch due to stack segment offset mapping conflict");
            break;
        default:
            throw AnalysisError("Unsupported segment register in instruction comparison: " + regName(segReg));
            break;
        }
        if (!match) return CMP_MISMATCH;
    }
    // now that offsets are checked we can accept a full match if there's one
    if (insResult == INS_MATCH_FULL) return CMP_MATCH;
    // instructions differ in value of immediate or memory offset
    assert(insResult == INS_MATCH_DIFF || insResult == INS_MATCH_DIFFOP1 || insResult == INS_MATCH_DIFFOP2);
    if (refInstr.isBranch()) { // call or jump (conditional/unconditional)
        const Branch 
            refBranch = getBranch(ref, refInstr, {}), 
            tgtBranch = getBranch(tgt, tgtInstr, {});
        if (refBranch.destination.isValid() && tgtBranch.destination.isValid()) {
            match = offMap.codeMatch(refBranch.destination, tgtBranch.destination);
            if (!match) {
                debug("Instruction mismatch on branch destination");
                return CMP_MISMATCH;
            }
            // TODO: perfect match... too perfect, mark as suspicious?
            if (refBranch.destination == tgtBranch.destination) return CMP_MATCH;
            // near jumps are usually used within a routine to handle looping and conditions,
            // so a different value (relative jump amount) might mean a wrong flow
            // -- mark with a different result value to be highlighted
            if (refInstr.isNearJump()) return CMP_DIFFTGT;
        }
        // proceed with regular operand comparison
    }
    // iterate over operands, either one or both could be different, so check if the difference is acceptable
    for (int opidx = 1; opidx <= 2; ++opidx) {
        const Instruction::Operand *op = nullptr;
        switch(opidx) {
        case 1: if (insResult != INS_MATCH_DIFFOP2) op = &refInstr.op1; break;
        case 2: if (insResult != INS_MATCH_DIFFOP1) op = &refInstr.op2; break;
        }
        if (op == nullptr) continue;
        // memory operand differences were already handled
        if (operandIsImmediate(op->type) && !options.strict) {
            debug("Ignoring immediate value difference in loose mode");
            // arbitrary heuristic to highlight small immediate value differences in red, these are usually suspicious
            if (op->dwordValue() <= 0xff && !refInstr.isBranch()) return CMP_DIFFTGT;
            return CMP_DIFFVAL;
        }
    }
    // all special cases exhausted, just a difference on the operand value
    return CMP_DIFFVAL;
}

void Analyzer::diffContext(const Executable &ref, const Executable &tgt) const {
    const int CONTEXT_COUNT = options.ctxCount;
    Address a1 = refCsip; 
    Address a2 = tgtCsip;
    verbose("--- Context information for up to " + to_string(CONTEXT_COUNT) + " additional instructions of routine " + output_color(OUT_RED) + routine.name + output_color(OUT_DEFAULT) 
        + " after mismatch location:");
    Instruction i1, i2;
    for (int i = 0; i <= CONTEXT_COUNT; ++i) {
        // make sure we are within code extents in both executables
        if (!ref.contains(a1) || !tgt.contains(a2)) break;
        i1 = Instruction{a1, ref.codePointer(a1)},
        i2 = Instruction{a2, tgt.codePointer(a2)};
        if (i != 0) verbose(compareStatus(i1, i2, true));
        a1 += i1.length;
        a2 += i2.length;
    }
}

// display skipped instructions after a definite match or mismatch found, impossible to display as instructions are scanned because we don't know
// how many will be skipped in advance
void Analyzer::skipContext(const Executable &ref, const Executable &tgt) const {
    Address 
        refAddr = refSkipOrigin,
        tgtAddr = tgtSkipOrigin; 
    Size 
        refSkipped = refSkipCount,
        tgtSkipped = tgtSkipCount;
    while (refSkipped > 0 || tgtSkipped > 0) {
        Instruction refInstr, tgtInstr;
        if (refSkipped > 0) {
            refInstr = {refAddr, ref.codePointer(refAddr)};
            refSkipped--;
            refAddr += refInstr.length;
        }
        if (tgtSkipped > 0) {
            tgtInstr = {tgtAddr, tgt.codePointer(tgtAddr)};
            tgtSkipped--;
            tgtAddr += tgtInstr.length;
        }
        verbose(output_color(OUT_YELLOW) + compareStatus(refInstr, tgtInstr, true, INS_MATCH_DIFF) + " [skip]" + output_color(OUT_DEFAULT));
    }
}

void Analyzer::calculateStats(const CodeMap &routineMap) {
    if (routineMap.empty()) return;
    routineSumSize = reachableSize = unreachableSize = excludedSize = excludedCount = excludedReachableSize = missedSize = ignoredSize = 0;
    missedNames.clear();
    for (Size i = 0; i < routineMap.routineCount(); i++) {
        // gather static routine map stats
        const Routine r = routineMap.getRoutine(i);
        routineSumSize += r.size();
        reachableSize += r.reachableSize();
        unreachableSize += r.unreachableSize();
        if (r.ignore || (r.assembly && !options.checkAsm)) {
            excludedCount++;
            excludedSize += r.size();
            excludedReachableSize += r.reachableSize();
        }
        // check against routines which we actually visited at runtime
        // report routines in map which were not visited as missed, unless they were assembly 
        // and we are not in checkAsm mode
        else if (routineNames.count(r.name) == 0 && (!r.assembly || options.checkAsm)) {
            missedSize += r.size();
            missedNames.insert(r.name);
        }
        // sum up actual encountered but ignored area during comparison
        if (excludedNames.count(r.name)) {
            ignoredSize += r.size();
        }
    }    
}

// display comparison statistics
void Analyzer::comparisonSummary(const Executable &ref, const CodeMap &routineMap, const bool showMissed) {
    // TODO: display total size of code segments between load module and routine map size
    if (routineMap.empty()) return;
    const Size 
        visitedCount = routineNames.size(),
        ignoredCount = excludedNames.size(),
        routineMapSize = routineMap.routineCount(),
        loadModuleSize = ref.size();

    calculateStats(routineMap);

    ostringstream msg;
    msg << "--- Routine map stats (static):" << endl
        << "Load module of executable is " << sizeStr(loadModuleSize) << " bytes" << endl
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
        if (showMissed) for (const auto &n : missedNames) {
            const Routine r = routineMap.getRoutine(n);
            if (!r.isValid()) throw LogicError("Unable to find missed routine " + n + " in routine map");
            msg << endl << r.dump(false);
        }
    }
    msg << endl 
        << "(*) Any routines called only by ignored routines have not been seen and will lower the practical score," << endl 
        << "    but theoretically if we stepped into ignored routines, we would have seen and ignored any that were excluded.";
    verbose(msg.str());
}

void Analyzer::processDataReference(const Executable &exe, const Instruction i, const CpuState &regs) {
    const SOffset off = i.memOffset();
    // ignore NULL
    if (off == 0) return;
    assert(off <= WORD_MAX);
    Word offVal = static_cast<Word>(off);
    // TODO: no idea what to do with negative offsets for now
    if (offVal < 0) {
        debug("Data reference with negative offset: " + signedHexVal(static_cast<SWord>(offVal)) + ", ignoring");
        return;
    }
    Register segReg = REG_DS;
    // handle segment override in instruction to be able to locate data in the middle of code, not sure if stack makes sense but whatever
    if (prefixIsSegment(i.prefix)) segReg = prefixRegId(i.prefix);
    // read value of appropriate segment if possible
    if (!regs.isKnown(segReg)) {
        debug("Data reference encountered with offset " + hexVal(offVal) + ", but value of " + regName(segReg) + " unknown - ignoring");
        return;
    }
    const Word dsAddr = regs.getValue(segReg);
    Address dataAddr{dsAddr, offVal};
    vars.insert({"", dataAddr});
    debug("Storing data reference: " + dataAddr.toString() + ", " + regName(segReg) + " = " + hexVal(regs.getValue(segReg)));
}

struct Duplicate {
    Distance distance;
    Size refSize, tgtSize, dupIdx;
    vector<Block> dupBlocks;

    Duplicate(const Distance distance, const Size refSize, const Size tgtSize, const Size dupIdx) : distance(distance), refSize(refSize), tgtSize(tgtSize), dupIdx(dupIdx) {}
    Duplicate(const Distance distance) : Duplicate(distance, 0, 0, BAD_ROUTINE) {}
    Duplicate() : Duplicate(MAX_DISTANCE) {}
    bool isValid() const { return distance != MAX_DISTANCE; }
    string toString() const {
        ostringstream str;
        str << "distance = " << distance << ", size ref: " << refSize << " tgt: " << tgtSize << ", idx: " << dupIdx;
        return str.str();
    }
};

bool Analyzer::findDuplicates(const SignatureLibrary signatures, Executable &tgt, CodeMap &tgtMap) {
    if (signatures.empty()) throw ArgError("Empty signature library provided for duplicate search");
    if (tgtMap.empty()) throw ArgError("Empty routine map provided for duplicate search");
    info("Searching for duplicates of " + to_string(signatures.signatureCount()) + " signatures among " + to_string(tgtMap.routineCount()) + " candidates, minimum instructions: " + to_string(options.routineSizeThresh) + ", maximum distance ratio: " + to_string(options.routineDistanceThresh) + "%");
    // store relationship between reference routines and their duplicates
    map<RoutineIdx, Duplicate> duplicates;
    Size ignoreCount = 0, ignoreTotalInstr = 0, missCount = 0, sigTotalInstr = 0, tgtTotalInstr = 0, missTotalInstr = 0;
    bool collision = false;
    // iterate over routines to find duplicates for
    for (Size sigIdx = 0; sigIdx < signatures.signatureCount(); ++sigIdx) {
        const SignatureItem &sig = signatures.getSignature(sigIdx);
        debug("Processing signature for routine: " + sig.routineName);
        const Size sigSize = sig.size();
        sigTotalInstr += sigSize;
        if (sigSize < options.routineSizeThresh) {
            verbose("[" + to_string(sigIdx+1) + "/" + to_string(signatures.signatureCount()) + "] Ignoring signature " + sig.routineName + ", instruction count below threshold: " + to_string(sigSize));
            ignoreCount++;
            ignoreTotalInstr += sigSize;
            continue;
        }
        // calculate max edit distance threshold to use as ratio of the signature size, so that larger routines can have more differing instructions
        const Size distanceThresh = (sigSize * options.routineDistanceThresh) / 100;
        // seed lowest distance found
        duplicates[sigIdx] = Duplicate{MAX_DISTANCE};
        debug("Searching for duplicates of routine " + sig.routineName + ", got string of " + to_string(sigSize) + " instructions, thresh = " + to_string(distanceThresh));
        // iterate over signatures in library
        bool have_dup = false;
        Size tgtTotalTemp = 0;
        for (Size tgtIdx = 0; tgtIdx < tgtMap.routineCount(); ++tgtIdx) {
            Routine tgtRoutine = tgtMap.getRoutine(tgtIdx);
            // TODO: try other reachable blocks?
            const Block tgtBlock = tgtRoutine.mainBlock();
            if (!tgtBlock.isValid()) {
                debug("Routine has no valid block: " + tgtRoutine.toString());
                continue;
            }
            // extract string of signatures for target routine
            vector<Signature> tgtSigs = tgt.getSignatures(tgtBlock);
            const auto tgtSigSize = tgtSigs.size();
            if (tgtTotalInstr == 0) tgtTotalTemp += tgtSigSize;
            const Size sigDelta = sigSize > tgtSigSize ? sigSize - tgtSigSize : tgtSigSize - sigSize;
            // ignore candidate if we know in advance the distance will be too high based on instruction count alone
            if (sigDelta > distanceThresh) {
                debug("\tIgnoring target routine " + tgtRoutine.name + " (" + to_string(tgtSigSize) + " instructions), instruction count difference exceeds distance threshold: " + to_string(sigDelta));
                continue;
            }
            // calculate edit distance between reference and target signature strings
            const auto distance = edit_distance_dp_thr(sig.signature.data(), sigSize, tgtSigs.data(), tgtSigSize, distanceThresh);
            if (distance > distanceThresh) {
                debug("\tIgnoring target routine " + tgtRoutine.name + " (" + to_string(tgtSigSize) + " instructions), distance above threshold");
                continue;
            }
            debug("\tPotential duplicate found: " + tgtRoutine.name + " (" + to_string(tgtSigSize) + " instructions), distance: " + to_string(distance) + " instructions");
            Duplicate &d = duplicates[sigIdx];
            if (distance < d.distance) {
                have_dup = true;
                debug("\tCalculated distance below previous value of " + to_string(d.distance));
                d = Duplicate{distance, sigSize, tgtSigSize, tgtIdx};
                d.dupBlocks.push_back(tgtBlock);
                debug("\tStored duplicate: " + duplicates[sigIdx].toString());
            }
            else debug("\tIgnoring target routine " + tgtRoutine.name + " (" + to_string(tgtSigSize) + " instructions), distance above previous value of " + to_string(d.distance));
        } // iterate over target routines
        if (tgtTotalInstr == 0) tgtTotalInstr = tgtTotalTemp;
        // found an eligible duplicate after going through all target routines
        if (have_dup) {
            Duplicate &curDup = duplicates[sigIdx];
            debug("Processing duplicate: " + curDup.toString());
            const Routine dupRoutine = tgtMap.getRoutine(curDup.dupIdx);
            verbose("[" + to_string(sigIdx+1) + "/" + to_string(signatures.signatureCount()) + "] Found duplicate of routine " + sig.routineName + " (" + to_string(curDup.refSize) + " instructions): " 
                + dupRoutine.toString() + " (" + to_string(curDup.tgtSize) + " instructions) with distance " + to_string(curDup.distance));
            // walk over all found duplicates looking for collisions
            for (auto& [otherSigIdx, otherDup] : duplicates) {
                if (otherDup.dupIdx != curDup.dupIdx || otherSigIdx == sigIdx) continue;
                // target routine which we just identified as a duplicate of the currently processed reference exe routine was already marked a duplicate of another
                collision = true;
                Size clearIdx;
                const SignatureItem &otherSig = signatures.getSignature(otherSigIdx);
                // current duplicate better than other, clear other
                if (curDup.distance < otherDup.distance) {
                    warn("Unmarking " + dupRoutine.toString() + " as duplicate of " + otherSig.routineName + ", current distance " + to_string(curDup.distance) 
                        + " better than " + to_string(otherDup.distance), OUT_YELLOW);
                    otherDup = {};
                }
                // current duplicate worse than other, clear current
                else if (curDup.distance > otherDup.distance) {
                    warn("Ignoring " + dupRoutine.toString() + " as duplicate of " + sig.routineName + " with distance " + to_string(curDup.distance) + ", previously better matched by " + otherSig.routineName
                        + " with distance " + to_string(otherDup.distance), OUT_YELLOW);
                    clearIdx = sigIdx;
                    curDup = {};
                }
                // current duplicate as good as other, keep both for now
                else {
                    warn("Routine " + dupRoutine.toString() + " is a duplicate of " + sig.routineName + " and " + otherSig.routineName + " with equal distance", OUT_BRIGHTRED);
                    continue;
                }
            }
        }
        else {
            verbose("[" + to_string(sigIdx+1) + "/" + to_string(signatures.signatureCount()) + "] Unable to find duplicate of " + sig.routineName + ", " + to_string(sigSize) + " instructions, max distance " + to_string(distanceThresh));
            missCount++;
            missTotalInstr += sigSize;
        }
    } // iterate over reference routines

    // final run through the duplicates to patch the output map
    Size dupCount = 0, dupTotalInstr = 0;
    std::set<Size> dupIdxs;
    for (const auto& [sigIdx, dup] : duplicates) {
        if (!dup.isValid()) continue;
        dupIdxs.insert(dup.dupIdx);
        const SignatureItem &origSig = signatures.getSignature(sigIdx);
        Routine &dupRoutine = tgtMap.getMutableRoutine(dup.dupIdx);
        debug("Emitting duplicate: " + dupRoutine.toString());
        // TODO: list duplicate blocks once figured out how to do comparisons on other blocks than the main one
        Block extents = dup.dupBlocks.front();
        extents.rebase(tgt.loadAddr().segment);
        dupRoutine.addComment("Routine " + dupRoutine.name + " " + extents.toString() + " is a potential duplicate of routine " + origSig.routineName 
            + " " + origSig.routineExtents.toString()  + ", differs by " + to_string(dup.distance) + " instructions");
        dupRoutine.duplicate = true;
        dupCount++;
        // sum up total number of instructions in detected duplicate routines
        dupTotalInstr += dup.tgtSize;
    }
    const Size uniqueDups = dupIdxs.size();

    if (dupCount) {
        const Size dupPercent = (dupTotalInstr * 100) / tgtTotalInstr;
        info("Processed " + to_string(signatures.signatureCount()) + " signatures, ignored " + to_string(ignoreCount) + " as too short\n" +
             "Tried to find matches for " + to_string(tgtMap.routineCount()) + " routines of target executable (" + tgt.path() + "), " + to_string(tgtTotalInstr) + " instructions total, 100%\n" +
             "Found " + to_string(dupCount) + " (unique: " + to_string(uniqueDups) + ") matching routines, " + to_string(dupTotalInstr) + " instructions total, " + output_color(OUT_GREEN) + 
             to_string(dupPercent) + "%" + output_color(OUT_DEFAULT) + "\n" +
             "Unable to find " + to_string(tgtMap.routineCount() - dupCount) + " matching routines, " + output_color(OUT_RED) + to_string(100 - dupPercent) + "%" + output_color(OUT_DEFAULT));
    }
    else info("No duplicates found, ignored " + to_string(ignoreCount) + " routines", OUT_RED);

    if (collision) {
        assert(uniqueDups < dupCount);
        warn("Some routines were found as duplicates of more than one routine. This is possible, but unlikely. Try using a longer minimum routine size and/or lower distance threshold to avoid false positives.", OUT_BRIGHTRED);
    }

    return dupCount != 0;
}

// brute force search for potential locations where addresses of variables might be located in the executable, these should be changed from hardcoded numbers to proper pointers/references when reconstructing the executable
void Analyzer::findDataRefs(const Executable &exe, const CodeMap &map) {
    const auto &segments = map.getSegments();
    const Size 
        codeSize = exe.size(),
        segCount = segments.size(),
        varCount = map.variableCount();
    Size refCount = 0;
    Offset startOffset = 0, endOffset = 0, finalOffset = exe.loadAddr().toLinear() + codeSize;
    const Byte *code = exe.codePointer(Address{0});
    struct VarRef {
        string segname;
        int count;
        Offset offset;
    };
    std::map<string, VarRef> varRefs;
    if (varCount == 0) throw AnalysisError("Map does not contain any variable locations");
    for (Size si = 0; si < segCount; ++si) {
        const Segment seg = segments[si];
        // TODO: consider searching for data refs in code segments, maybe at least in unclaimed blocks?
        if (seg.type != Segment::SEG_DATA) {
            debug("Ignoring non-data segment " + seg.toString());
            continue;
        }
        startOffset = SEG_TO_OFFSET(seg.address);
        if (si < segCount - 1) { // not last segment
            const Segment nextSeg = segments[si + 1];
            endOffset = SEG_TO_OFFSET(nextSeg.address);
            debug("Obtained end offset from next segment: " + nextSeg.toString() + ": " + hexVal(endOffset));
        }
        else {
            endOffset = finalOffset - 1;
            debug("Last segment, obtained end offset from code size: " + hexVal(endOffset));
        }
        if (endOffset >= finalOffset)  {
            endOffset = finalOffset - 1;
            debug("Corrected end offset to load module end: " + hexVal(endOffset));
        }
        debug("Now searching in segment " + seg.toString() + ", start at " + hexVal(startOffset) + ", end at " + hexVal(endOffset));
        for (Offset off = startOffset; off < endOffset - 1; ++off) {
            if (off + 1 >= finalOffset) throw AnalysisError("Stepped out of executable load module bounds at " + hexVal(off));
            const Word val = (code[off + 1] << 8) | code[off];
            if (val == 0) continue; // no point to find offset zero
            debug("Value at offset " + hexVal(off) + "/" + hexVal(off - startOffset) + ": " + hexVal(val));
            for (Size vi = 0; vi < varCount; ++vi) {
                const Variable v = map.getVariable(vi);
                if (v.addr.offset == val) {
                    verbose(seg.name + "/" + hexVal(off - startOffset) + ": potential reference to variable " + v.toString());
                    refCount++;
                    if (varRefs.count(v.name)) varRefs[v.name].count++;
                    else varRefs[v.name] = { seg.name, 1, off - startOffset };
                }
            } // iterate over known variables
        } // iterate over bytes within segment
    } // iterate over segments
    info("Search complete, found " + to_string(refCount) + " potential references, unique: " + to_string(varRefs.size()));
    vector<pair<string, VarRef>> sortedRefs(varRefs.size(), pair<string, VarRef>());
    std::copy(varRefs.begin(), varRefs.end(), sortedRefs.begin());
    std::sort(sortedRefs.begin(), sortedRefs.end(), [](const auto &p1, const auto &p2){
        // sort by reference count first, then by reference offset
        if (p1.second.count == p2.second.count) {
            return p1.second.offset < p2.second.offset;
        }
        return p1.second.count < p2.second.count;
    });
    info("Printing reference counts per variable, counts higher than 1 or 2 are probably false positives due to a low/non-characteristic offset");
    for (const auto &p : sortedRefs) {
        const auto v = map.getVariable(p.first);
        if (p.second.count == 1) {
            info(v.toString() + ": 1 reference @ " + p.second.segname + ":" + hexVal(p.second.offset));
        }
        else info(v.toString() + ": " + to_string(p.second.count) + " references");
    }
}