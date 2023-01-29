#include "dos/analysis.h"
#include "dos/opcodes.h"
#include "dos/instruction.h"
#include "dos/output.h"
#include "dos/util.h"
#include "dos/error.h"

#include <iostream>
#include <sstream>
#include <stack>
#include <algorithm>
#include <fstream>
#include <string>
#include <cstring>
#include <list>

using namespace std;

static void debug(const string &msg) {
    output(msg, LOG_ANALYSIS, LOG_DEBUG);
}

static void info(const string &msg) {
    output(msg, LOG_ANALYSIS, LOG_INFO);
}

static void error(const string &msg) {
    output(msg, LOG_ANALYSIS, LOG_ERROR);
}

bool Routine::contains(const Address &addr) const {
    if (extents.contains(addr)) return true;
    for (const auto &c : chunks) {
        if (c.contains(addr)) return true;
    }
    return false;
}

string Routine::toString() const {
    ostringstream str;
    str << extents.toString(true) << ": " << name << " / " << to_string(id);
    for (const auto &c : chunks) {
        str << endl << "\tfunction chunk: " << c.toString(true);
    }
    return str.str();
}

// create routine map from IDA .lst file
// TODO: ida puts the procedure end on the instruction start, get next offset and correct
RoutineMap::RoutineMap(const std::string &path) {
    const auto fstat = checkFile(path);
    if (!fstat.exists) throw ArgError("File does not exist: "s + path);
    debug("Loading IDA routine map from "s + path);
    ifstream fstr{path};
    string line, lastAddr;
    Size lineno = 1;
    Size routineCount = 0;
    Address curAddr, endpAddr;
    while (safeGetline(fstr, line)) {
        istringstream sstr{line};
        string token[4];
        Size tokenno = 0;
        while (tokenno < 4 && sstr >> token[tokenno]) {
            tokenno++;
        }
        string &addrStr = token[0];
        if (addrStr.find("seg000") != string::npos) {
            addrStr = addrStr.substr(2,9);
            addrStr[0] = '0';
            curAddr = Address{addrStr};
        }
        else {
            curAddr = Address();
        }

        // close previous routine if we encountered an endp and the address 
        if (endpAddr.isValid() && curAddr != endpAddr) {
            auto &r = routines.back();
            r.extents.end = curAddr - 1; // go one byte before current address
            debug("Closing routine "s + r.name + ", id " + to_string(r.id) + " @ " + r.extents.end.toString());
            endpAddr = Address(); // makes address invalid
        }
        // start new routine
        if (token[2] == "proc") { 
            routines.emplace_back(Routine(token[1], ++routineCount, curAddr));
            const auto &r = routines.back();
            debug("Found start of routine "s + r.name + ", id " + to_string(r.id) + " @ " + r.extents.begin.toString());
        }
        // routine end, but IDA places endp at the offset of the beginning of the last instruction, 
        // and we want the end to include the last instructions' bytes, so just memorize the address and close the routine later
        else if (token[2] == "endp") { 
            endpAddr = curAddr;
            debug("Found end of routine @ " + endpAddr.toString());
        }
        lineno++;
    }
    debug("Done, found "s + to_string(routines.size()) + " routines");
}

void RoutineMap::dump() const {
    if (empty()) {
        info("--- Empty routine map --- ");
        return;
    }

    // display routines
    for (const auto &r : routines) {
        info(r.toString());
    }
}

Size RoutineMap::match(const RoutineMap &other) const {
    Size matchCount = 0;
    for (const auto &r : routines) {
        bool routineMatch = false;
        for (const auto &ro : other.routines) {
            if (r.extents == ro.extents) {
                debug("Found routine match for "s + r.toString() + " with " + ro.toString());
                routineMatch = true;
                matchCount++;
                break;
            }
        }
        if (!routineMatch) {
            debug("Unable to find match for "s + r.toString());
        }
    }
    return matchCount;
}

// register a block either as routine main body, or chunk
void RoutineMap::addBlock(const Block &b, const int id) {
    auto existingRoutine = find_if(routines.begin(), routines.end(), [id](const Routine &r){
        return r.id == id;
    });
    if (existingRoutine == routines.end()) {
        debug("Creating new routine for id "s + to_string(id) + " with main body spanning " + b.toString());
        routines.emplace_back(Routine("routine_"s + to_string(id), id, b));
    }
    else {
        debug("Routine with id "s + to_string(id) + " already exists spanning " + existingRoutine->extents.toString() + ", adding " + b.toString() + " as chunk");
        existingRoutine->chunks.push_back(b);
    }
}

Executable::Executable(const MzImage &mz) : entrypoint(mz.entrypoint()), reloc(mz.loadSegment())  {
    const Byte *data = mz.loadModuleData();
    copy(data, data + mz.loadModuleSize(), std::back_inserter(code));
}

struct SearchPoint {
    Address address;
    int routineId;
    bool isCall;
    SearchPoint() : routineId(0), isCall(false) {}
    SearchPoint(const Routine &r) : address(r.entrypoint()), routineId(r.id), isCall(true) {}
    SearchPoint(const Address address, const int id, const bool call) : address(address), routineId(id), isCall(call) {}
    bool match(const SearchPoint &other) const {
        return address == other.address && isCall == other.isCall;
    }
    string toString() const {
        ostringstream str;
        str << "[" << address.toString() << " / " << routineId << " / " << (isCall ? "call" : "jump") << "]";
        return str.str();
    }
    bool isNull() const { return address.isNull(); }
};

// utility class for keeping track of the queue of potentially interesting locations (jump/call targets), and which instruction bytes have been claimed by a routine
class SearchQueue {
    // memory map for marking which locations belong to which routines, value of 0 is undiscovered
    vector<int> visited; // TODO: store addresses from loaded exe in map, otherwise they don't match after analysis done    
    list<SearchPoint> sq;
    SearchPoint curSearch;
    int lastRoutineId;

public:
    SearchQueue(const Size codeSize, const SearchPoint &seed) : visited(codeSize, NULL_ROUTINE), lastRoutineId(seed.routineId) {
        sq.push_front(seed);
    }
    Size size() const { return sq.size(); }
    bool empty() const { return sq.empty(); }
    void message(const Address &a, const string &msg, const LogPriority pri = LOG_VERBOSE) {
        output("[r"s + to_string(curSearch.routineId) + "/q" + to_string(size()) + "]: " + a.toString() + ": " + msg, LOG_ANALYSIS, pri);
    }
    int getRoutine(const Offset off) const {return visited.at(off); }

    void markRoutine(const Offset off, const Size length, int id = NULL_ROUTINE) {
        if (id == NULL_ROUTINE) id = curSearch.routineId;
        fill(visited.begin() + off, visited.begin() + off + length, id);
    }

    SearchPoint nextPoint() {
        if (!empty()) {
            curSearch = sq.front();
            sq.pop_front();
        }
        return curSearch;
    }

    bool hasPoint(const Address &dest, const bool call) {
        SearchPoint findme(dest, 0, call);
        const auto it = find_if(sq.begin(), sq.end(), [&](const SearchPoint &p){
            // debug(p.toString() + " == " + findme.toString() + " ?");
            return p.match(findme);
        });
        if (it != sq.end()) {
            debug("search queue already contains matching point at "s + it->address.toString() + " belonging to routine " + to_string(it->routineId));
            return true;
        }
        return false;
    };

    // place a jump (conditional jump or call) into the search queue to be investigated later
    void savePoint(const Address &src, const Address &dest, const bool isCall) {
        const Offset destOff = dest.toLinear();
        const int destId = getRoutine(destOff);
        if (isCall) { 
            // function call, create new routine at destination if either not visited, 
            // or visited but previous location is marked with the same routine id as the call location, 
            // meaning this was not discovered as a routine entrypoint and this call now takes precedence and will replace it
            if ((destId == NULL_ROUTINE || (destOff > 0 && getRoutine(destOff - 1) == destId)) && !hasPoint(dest, isCall)) {
                lastRoutineId++;
                if (destId == NULL_ROUTINE) debug("call destination not belonging to any routine, saving as search point for new routine " + to_string(lastRoutineId));
                else debug("call destination internal to routine " + to_string(destId) + ", saving as search point for new routine " + to_string(lastRoutineId));
                sq.emplace_front(SearchPoint(dest, lastRoutineId, true));
            }
        }
        else { 
            // conditional jump, save as destination to be investigated, belonging to current routine
            if (destId == NULL_ROUTINE && !hasPoint(dest, isCall)) {
                debug("jump destination not belonging to any routine "s + dest.toString() + ", saving as search point for existing routine " + to_string(curSearch.routineId));
                sq.emplace_front(SearchPoint(dest, curSearch.routineId, false));
            }
        }
    }

    RoutineMap makeMap() const {
        RoutineMap map;
        // dump map to file for debugging
        ofstream mapFile("analysis.map");
        mapFile << "      ";
        for (int i = 0; i < 16; ++i) mapFile << hex << setw(5) << setfill(' ') << i << " ";
        mapFile << endl;

        info("Building routine map from analysis results");
        int prev_id = NULL_ROUTINE;
        Offset blockStart = 0;
        for (size_t mapOffset = 0; mapOffset < visited.size(); ++mapOffset) {
            const int id = getRoutine(mapOffset);
            //debug(hexVal(mapOffset) + ": " + to_string(m));
            if (mapOffset % 16 == 0) {
                if (mapOffset != 0) mapFile << endl;
                mapFile << hex << setw(5) << setfill('0') << mapOffset << " ";
            }
            mapFile << dec << setw(5) << setfill(' ') << id << " ";

            if (id != prev_id) { // new block begins
                if (prev_id != NULL_ROUTINE) { // need to close previous block
                    Block block{Address(blockStart), Address(mapOffset - 1)};
                    debug(hexVal(mapOffset) + ": closing block " + block + " for routine " + to_string(prev_id));
                    map.addBlock(block, prev_id);
                }
                // record block start
                if (id != NULL_ROUTINE) {
                    debug(hexVal(mapOffset) + ": starting block for routine " + to_string(id));
                    blockStart = mapOffset;
                }
            }
            prev_id = id;
        }
        info("Found " + to_string(map.routines.size()) + " routines");
        // sort routines by entrypoint
        std::sort(map.routines.begin(), map.routines.end(), [](const Routine &a, const Routine &b){
            return a.entrypoint().toLinear() < b.entrypoint().toLinear();
        });
        // sort chunks within routines by begin address
        for (auto &r : map.routines) {
            sort(r.chunks.begin(), r.chunks.end(), [](const Block &b1, const Block &b2){
                return b1.begin < b2.begin;
            });
        }
        return map;
    }
};

void setRegValue(const Registers &regs, const Register regid, const Instruction::Operand &source) {

}

// explore the code without actually executing instructions, discover routine boundaries
// TODO: trace modifications to CS, support multiple code segments
// TODO: trace other register values for discovering register-dependent calls?
// TODO: identify routines through signatures generated from OMF libraries
RoutineMap findRoutines(const Executable &exe) {
    const Byte *code = exe.code.data();
    const Size codeSize = exe.code.size();
    const Address entrypoint = exe.entrypoint;
    const Block codeExtents = Block({entrypoint.segment, 0}, Address(SEG_OFFSET(entrypoint.segment) + codeSize));
    // queue for BFS search
    SearchQueue searchQ(codeSize, SearchPoint(entrypoint, 1, true));
    Registers regs;
    Byte memByte;
    Word memWord;

    info("Analyzing code in range " + codeExtents);
    // iterate over entries in the search queue
    while (!searchQ.empty()) {
        // get a location from the queue and jump to it
        const SearchPoint search = searchQ.nextPoint();
        Address csip = search.address;
        const int rid = searchQ.getRoutine(csip.toLinear());
        searchQ.message(csip, "starting search at new location, call: "s + to_string(search.isCall));
        if (rid != NULL_ROUTINE && !search.isCall) {
            debug("location already claimed by routine "s + to_string(rid) + " and search point did not originate from a call, skipping");
            continue;
        }
        bool scan = true;
        // iterate over instructions at current search location in a linear fashion, until an unconditional jump or return encountered
        while (scan) {
            if (!codeExtents.contains(csip)) {
                searchQ.message(csip, "ERROR: advanced past loaded code extents", LOG_ERROR);
                return {};
            }
            const Offset instrOffs = csip.toLinear();
            Instruction i(csip, code + instrOffs);
            //analysisMessage("Instruction at offset "s + hexVal(instrOffs) + ": " + i.toString());
            // cpuMessage(regs_.csip().toString() +  ": opcode " + hexVal(opcode_) + ", instrLen = " + to_string(instrLen));
            // mark memory map items corresponding to the current instruction as belonging to the current routine
            searchQ.markRoutine(instrOffs, i.length);
            Address jumpAddr = csip;
            bool call = false;
            // interpret the instruction
            switch (i.iclass) {
            // jumps
            case INS_JMP:
                // conditional jump, put destination into search queue to check out later
                if (opcodeIsConditionalJump(i.opcode)) {
                    jumpAddr.offset = i.relativeOffset();
                    searchQ.message(csip, "encountered conditional near jump to "s + jumpAddr.toString());
                }
                else switch (i.opcode) {
                // unconditional jumps, cannot continue at current location, need to jump now
                case OP_JMP_Jb: 
                case OP_JMP_Jv: 
                    jumpAddr.offset = i.relativeOffset();
                    searchQ.message(csip, "encountered unconditional near jump to "s + jumpAddr.toString());
                    scan = false;
                    break;
                case OP_GRP5_Ev:
                    searchQ.message(csip, "unknown near jump target: "s + i.toString());
                    break; 
                default: 
                    throw AnalysisError("Unsupported jump opcode: "s + hexVal(i.opcode));
                }
                break;
            case INS_JMP_FAR:
                if (i.op1.type == OPR_IMM32) {
                    jumpAddr = Address{i.op1.immval.u32}; 
                    searchQ.message(csip, "encountered unconditional far jump to "s + jumpAddr.toString());
                    scan = false;
                }
                else searchQ.message(csip, "unknown far jump target: "s + i.toString());
                break;
            // loops
            case INS_LOOP:
            case INS_LOOPNZ:
            case INS_LOOPZ:
                jumpAddr.offset = i.relativeOffset();
                searchQ.message(csip, "encountered loop to "s + jumpAddr.toString());
                break;
            // calls
            case INS_CALL:
                if (i.op1.type == OPR_IMM16) {
                    jumpAddr.offset = i.relativeOffset();
                    searchQ.message(csip, "encountered near call to "s + jumpAddr.toString());
                    call = true;
                }
                else if (operandIsReg(i.op1.type)) {
                    jumpAddr.offset = regs.bit16(i.op1.regId());
                    searchQ.message(csip, "[XXX] encountered near call through register to "s + jumpAddr.toString());
                }
                else if (operandIsMemImmediate(i.op1.type)) {
                    Address a{regs.bit16(REG_DS), i.op1.immval.u16};
                    memcpy(&memWord, code + a.toLinear(), sizeof(Word));
                    jumpAddr.offset = memWord;
                    searchQ.message(csip, "[XXX] encountered near call through mem pointer to "s + jumpAddr.toString());
                }
                else searchQ.message(csip, "unknown near call target: "s + i.toString());
                break;
            case INS_CALL_FAR:
                if (i.op1.type == OPR_IMM32) {
                    jumpAddr = Address(i.op1.immval.u32);
                    searchQ.message(csip, "encountered far call to "s + jumpAddr.toString());
                    call = true;
                }
                else searchQ.message(csip, "unknown far call target: "s + i.toString());
                break;
            // returns
            case INS_RET:
            case INS_RETF:
            case INS_IRET:
                searchQ.message(csip, "returning from routine");
                scan = false;
                break;
            // track some movs to gather some jump/call targets from register values
            // TODO: store regs' values and known/unknown state in queue with SearchPoint, pop when searching at new location
            case INS_MOV:
                if (operandIsReg(i.op1.type)) {
                    const Register dest = i.op1.regId();
                    bool set = false;
                    if (operandIsImmediate(i.op2.type)) {
                        switch(i.op2.size) {
                        case OPRSZ_BYTE: 
                            debug("setting reg8 "s + to_string(dest) + " to imm " + hexVal(i.op2.immval.u8));
                            regs.bit8(dest) = i.op2.immval.u8; 
                            debug("value = "s + hexVal(regs.bit8(dest)));
                            set = true; 
                            break;
                        case OPRSZ_WORD: 
                            regs.bit16(dest) = i.op2.immval.u16;
                            set = true; 
                            break;
                        }
                    }
                    else if (operandIsReg(i.op2.type)) {
                        const Register src = i.op2.regId();
                        switch(i.op2.size) {
                        case OPRSZ_BYTE: 
                            regs.bit8(dest) = regs.bit8(src);
                            set = true;
                            break;
                        case OPRSZ_WORD: 
                            regs.bit16(dest) = regs.bit16(src);
                            set = true;
                            break;
                        }
                    }
                    else if (operandIsMemImmediate(i.op2.type)) {
                        const Register base = prefixRegId(i.prefix);
                        Address a(regs.bit16(base), i.op2.immval.u16);
                        switch(i.op2.size) {
                        case OPRSZ_BYTE:
                            debug("setting reg8 "s + to_string(dest) + " to mem " + a.toString());
                            memByte = code[a.toLinear()];
                            regs.bit8(dest) = memByte; 
                            set = true;
                            break;
                        case OPRSZ_WORD: 
                            memcpy(&memWord, code + a.toLinear(), sizeof(Word));
                            regs.bit16(dest) = memWord; 
                            set = true;
                            break;
                        }
                    }
                    if (set) {
                        searchQ.message(csip, "[XXX] encountered move to register: "s + i.toString());
                        debug(regs.dump());
                    }
                }
                break;
            } // switch on instruction class

            // if the processed instruction was a jump or a call, store its entry in the search queue
            if (jumpAddr != csip) searchQ.savePoint(csip, jumpAddr, call);
            // advance to next instruction
            csip.offset += i.length;
        } // iterate over instructions
    } // next address from callstack
    info("Done analyzing code");

    // iterate over memory map that we built up, identify contiguous chunks belonging to routines
    int prev = NULL_ROUTINE, undiscoveredPrev = NULL_ROUTINE;
    Offset blockStart = 0;
    Block undiscoveredBlock;
    // find runs of undiscovered bytes surrounded by bytes discovered as belonging to the same routine and coalesce them with the routine
    // TODO: those areas are still practically undiscovered as we did not pass over their instructions, so we are missing paths leading from there,
    // perhaps a second pass over that area could discover more stuff?
    for (size_t mapOffset = codeExtents.begin.toLinear(); mapOffset < codeExtents.end.toLinear(); ++mapOffset) {
        const int m = searchQ.getRoutine(mapOffset);
        if (m == NULL_ROUTINE && (prev != NULL_ROUTINE || mapOffset == 0))  {
            undiscoveredBlock = Block{Address{mapOffset}}; // start new undiscovered block
            undiscoveredPrev = prev; // store id of routine preceeding the new block
        }
        else if (m != NULL_ROUTINE && undiscoveredPrev == m) { // end of undiscovered block and current routine matches the one before the block
            undiscoveredBlock.end = Address{mapOffset - 1};
            // overwrite undiscovered block with id of routine surrounding it
            searchQ.markRoutine(undiscoveredBlock.begin.toLinear(), undiscoveredBlock.size(), m);
        }
        prev = m;
    }

    // iterate over discovered memory map again and create routine map
    return searchQ.makeMap();
}

struct RoutinePair {
    Routine r1, r2;
    RoutinePair(const std::string &name, const int id, const Address &e1, const Address &e2) : r1{name, id, e1}, r2{name, id, e2} {}
    std::string toString() const {
        ostringstream str;
        str << "'" << r1.name << "', id " << to_string(r1.id) << ", entrypoints @ " << r1.entrypoint() << " <-> " << r2.entrypoint();
        return str.str();
    }
};

// TODO: implement operand correction for jmp, implement jmp around invalid code
bool compareCode(const Executable &base, const Executable &object) {
    const Byte 
        *bCode = base.code.data(),
        *oCode = object.code.data();
    const Size
        bSize = base.code.size(),
        oSize = object.code.size();
    Address
        bAddr = base.entrypoint,
        oAddr = object.entrypoint;
    list<RoutinePair> searchQ;
    vector<bool> visited(bSize, false);
    RoutinePair curRoutine{"start_"s + hexVal(base.entrypoint.toLinear(), false), 0, base.entrypoint, object.entrypoint};
    info("Starting in routine "s + curRoutine.toString());

    Size routineCount = 1;
    // TODO: check if address already visited, could encounter the same address again after it was already popped
    auto addCall = [&](const Address &a1, const Address &a2) {
        if (visited.at(a1.toLinear())) {
            info("Address "s + a1.toString() + " already visited");
            return;
        }
        auto found = find_if(searchQ.begin(), searchQ.end(), [&](const RoutinePair &arg){
            return arg.r1.entrypoint() == a1;
        });
        if (found != searchQ.end()) {
            assert(found->r2.entrypoint() == a2);
            info("Call to address "s + a1.toString() + " already present in search queue");
            return;
        }
        int lastId = 0;
        if (!searchQ.empty()) lastId = searchQ.back().r1.id;
        RoutinePair call{"routine_"s + hexVal(a1.toLinear(), false), ++lastId, a1, a2};
        info("Adding routine "s + call.toString() + " to search queue, size = " + to_string(searchQ.size()));
        searchQ.push_back(call);
        routineCount++;
    };

    // keep comparing instructions between the two executables (base and object) until a mismatch is found
    bool done = false;
    while (!done) {
        const Offset 
            bOffset = bAddr.toLinear(),
            oOffset = oAddr.toLinear();
        if (visited.at(bOffset)) {
            error("Address "s + bAddr.toString() + " already visited");
            return false;
        }
        if (bOffset >= bSize) {
            error("Passed base code boundary: "s + hexVal(bOffset));
            return false;
        }
        if (oOffset >= oSize) {
            error("Passed object code boundary: "s + hexVal(oOffset));
            return false;
        }

        Instruction bi{bAddr, bCode + bOffset}, oi{oAddr, oCode + oOffset};
        // mark instruction bytes as visited
        fill(visited.begin() + bOffset, visited.begin() + bOffset + bi.length, true);
        const InstructionMatch imatch = oi.match(bi);
        debug(hexVal(bOffset) + ": " + bi.toString() + " -> " + hexVal(oOffset) + ": " + oi.toString() + " [" + to_string(imatch) + "]");
        switch (imatch) {
        case INS_MATCH_FULL:
            break;
        case INS_MATCH_DIFFOP1:
        case INS_MATCH_DIFFOP2:
            info("Operand difference at offset "s + hexVal(bOffset) + " in base executable; has '" + bi.toString() 
                + "' as opposed to '" + oi.toString() + "' at offset " + hexVal(oOffset) + " in object executable");        
            break;
        case INS_MATCH_MISMATCH:
            info("Instruction mismatch at offset "s + hexVal(bOffset) + " in base executable; has '" + bi.toString() 
                + "' as opposed to '" + oi.toString() + "' at offset " + hexVal(oOffset) + " in object executable");
            return false;
        default:
            error("Invalid instruction match result: "s + to_string(imatch));
            return false;
        }

        bool jump = false;
        switch (bi.iclass) {
        case INS_JMP:
            if (opcodeIsConditionalJump(bi.opcode)) break;
            else {
                info("Encountered unconditional near jump to "s + bi.op1.toString() + " / " + oi.op1.toString());
                Address jumpAddr(bAddr.segment, bi.relativeOffset());
                if (visited.at(jumpAddr.toLinear())) { 
                    info("Address "s + jumpAddr.toString() + " already visited");
                    break;
                }
                switch (bi.op1.type) {
                case OPR_IMM16:
                    jump = true;
                    bAddr = jumpAddr;
                    oAddr.offset = oi.relativeOffset();
                    break;
                default:
                    info("Unknown jump target of type "s + to_string(bi.op1.type) + ": " + bi.op1.toString() + ", ignoring");
                }
            }
            break;
        case INS_JMP_FAR:
            error("Far jump not implemented");
            break;
        case INS_CALL:
            info("Encountered near call to "s + bi.op1.toString() + " / " + oi.op1.toString());
            assert(oi.op1.type == bi.op1.type);
            switch (bi.op1.type) {
            case OPR_IMM16:
                addCall(Address{bAddr.segment, bi.relativeOffset()}, Address{oAddr.segment, oi.relativeOffset()});
                break;
            default:
                info("Unknown call target of type "s + to_string(bi.op1.type) + ": " + bi.op1.toString() + ", ignoring");
            }
            break;
        case INS_CALL_FAR:
            error("Far call not implemented");
            break;
        case INS_RET:
        case INS_RETF:
        case INS_IRET:
            info("--- Encountered return from routine "s + curRoutine.toString());
            if (searchQ.empty()) {
                info("Nothing left on the search stack, concluding comparison");
                done = true;
            }
            else {
                curRoutine = searchQ.front();
                searchQ.pop_front();
                info("--- Proceeding to routine " + curRoutine.toString() + ", queue size = " + to_string(searchQ.size()));
                jump = true;
                bAddr = curRoutine.r1.entrypoint();
                oAddr = curRoutine.r2.entrypoint();
            }
            break;
        default: 
            // normal instruction
            break;
        }

        // move to the next instruction pair
        if (!jump) {
            bAddr += bi.length;
            oAddr += oi.length;
        }
    }

    info("Identified and compared "s + to_string(routineCount) + " routines");
    return true;
}