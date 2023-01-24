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
    string line;
    Size lineno = 1;
    Size routineCount = 0;
    while (safeGetline(fstr, line)) {
        istringstream sstr{line};
        string token[4];
        Size tokenno = 0;
        while (tokenno < 4 && sstr >> token[tokenno]) {
            tokenno++;
        }
        string &addrStr = token[0];
        string::size_type n;
        if ((n = addrStr.find("seg000")) != string::npos) {
            addrStr = addrStr.substr(2,9);
            addrStr[0] = '0';
        }

        if (token[2] == "proc") { // found routine
            routines.emplace_back(Routine(token[1], ++routineCount, Address{addrStr}));
            const auto &r = routines.back();
            debug("Found start of routine "s + r.name + ", id " + to_string(r.id) + " @ " + r.extents.begin.toString());
        }
        else if (token[2] == "endp") { // routine end
            auto &r = routines.back();
            r.extents.end = Address{addrStr};
            debug("Found end of routine "s + r.name + ", id " + to_string(r.id) + " @ " + r.extents.end.toString());
        }
        lineno++;
    }
    success = true;
    debug("Done, found "s + to_string(routines.size()) + " routines");
}

void RoutineMap::dump() const {
    if (!success) {
        error("Invalid routine map");
        return;
    }

    // display routines
    for (const auto &r : routines) {
        output(r.toString(), LOG_ANALYSIS, LOG_INFO);
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

static constexpr int NOT_VISITED = 0;

class SearchQueue {
    // memory map for marking which locations belong to which routines, value of 0 is undiscovered
    vector<int> visited; // TODO: store addresses from loaded exe in map, otherwise they don't match after analysis done    
    list<SearchPoint> sq;
    SearchPoint curSearch;
    int curRoutineId;

public:
    SearchQueue(const Size codeSize, const SearchPoint &seed) : visited(codeSize, NOT_VISITED), curRoutineId(1) {
        sq.push_front(seed);
    }

    Size size() const { 
        return sq.size(); 
    }

    bool empty() const {
        return sq.empty();
    }

    void message(const Address &a, const string &msg, const LogPriority pri = LOG_VERBOSE) {
        output("[r"s + to_string(curSearch.routineId) + "/q" + to_string(size()) + "]: " + a.toString() + ": " + msg, LOG_ANALYSIS, pri);
    }

    void markVisited(const Offset off, const Size length) {
        fill(visited.begin() + off, visited.begin() + off + length, curSearch.routineId);
    }

    SearchPoint nextPoint() {
        curSearch = sq.front();
        sq.pop_front();
        return curSearch;
    }

    bool hasPoint(const SearchPoint &findme) {
        const auto it = find_if(sq.begin(), sq.end(), [&](const SearchPoint &p){
            debug(p.toString() + " == " + findme.toString() + " ?");
            return p.match(findme);
        });
        if (it != sq.end()) {
            debug("search queue already contains matching point at "s + findme.address.toString() + " belonging to routine " + to_string(findme.routineId));
            return true;
        }
        return false;
    };

    // place a jump (conditional jump or call) into the search queue to be investigated later
    void savePoint(const Address &src, const Address &dest, const bool isCall) {
        const Offset destOff = dest.toLinear();
        const int destId = visited.at(destOff);
        if (isCall) { 
            // function call, create new routine at destination if either not visited, 
            // or visited but previous location is marked with the same routine id as the call location, 
            // meaning this was not discovered as a routine entrypoint and this call now takes precedence and will replace it
            if (destId == NOT_VISITED || (destOff > 0 && visited.at(destOff - 1) == destId)) {
                curRoutineId++;
                if (destId == NOT_VISITED) message(src, "discovered call to new routine at "s + dest.toString());
                else message(src, "overwriting location "s + dest.toString() + " belonging to routine " + to_string(destId) + " as entrypoint to new routine " + to_string(curRoutineId));
                auto sp = SearchPoint(dest, curRoutineId, true);
                if (!hasPoint(sp)) sq.push_front(sp);
            }
            else {
                message(src, "call destination already discovered as entrypoint to routine "s + to_string(destId));
            }
        }
        else { 
            // conditional jump, save as destination to be investigated, belonging to current routine
            if (destId == NOT_VISITED) {
                auto sp = SearchPoint(dest, curSearch.routineId, false);
                if (!hasPoint(sp)) sq.push_front(sp);
            }
            else {
                message(src, "jump destination already belongs to routine "s + to_string(destId));
            }
        }
    }
};

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

    info("Analyzing code in range " + codeExtents);
    // iterate over entries in the search queue
    while (!searchQ.empty()) {
        // get a location from the queue and jump to it
        const SearchPoint search = searchQ.nextPoint();
        Address csip = search.address;
        searchQ.message(csip, "starting search at new location");
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
            searchQ.markVisited(instrOffs, i.length);
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
                    searchQ.message(csip, "encountered call to "s + jumpAddr.toString());
                    call = true;
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
            } // switch on instruction class

            // if the processed instruction was a jump or a call, store its entry in the search queue
            if (jumpAddr != csip) searchQ.savePoint(csip, jumpAddr, call);
            // advance to next instruction
            csip.offset += i.length;
        } // iterate over instructions
    } // next address from callstack
    info("Done analyzing code");

#if 0
    // iterate over memory map that we built up, identify contiguous chunks belonging to routines
    int prev = NOT_VISITED, undiscoveredPrev = NOT_VISITED;
    Offset blockStart = 0;
    Block undiscoveredBlock;
    // find runs of undiscovered bytes surrounded by bytes discovered as belonging to the same routine and coalesce them with the routine
    // TODO: those areas are still practically undiscovered as we did not pass over their instructions, so we are missing paths leading from there,
    // perhaps a second pass over that area could discover more stuff?
    for (size_t mapOffset = codeExtents.begin.toLinear(); mapOffset < codeExtents.end.toLinear(); ++mapOffset) {
        const int m = visited.at(mapOffset);
        if (m == NOT_VISITED && (prev != NOT_VISITED || mapOffset == 0))  {
            undiscoveredBlock = Block{Address{mapOffset}}; // start new undiscovered block
            undiscoveredPrev = prev; // store id of routine preceeding the new block
        }
        else if (m != NOT_VISITED && undiscoveredPrev == m) { // end of undiscovered block and current routine matches the one before the block
            undiscoveredBlock.end = Address{mapOffset - 1};
            // overwrite undiscovered block with id of routine surrounding it
            fill(visited.begin() + undiscoveredBlock.begin.toLinear(), visited.begin() + undiscoveredBlock.end.toLinear(), m);
        }
        prev = m;
    }
#endif

    // iterate over discovered memory map again and create routine map
    RoutineMap ret;
    // dump map to file for debugging
    ofstream mapFile("analysis.map");
    mapFile << "      ";
    for (int i = 0; i < 16; ++i) mapFile << hex << setw(5) << setfill(' ') << i << " ";
    mapFile << endl;

#if 0
    prev = NOT_VISITED;
    for (size_t mapOffset = codeExtents.begin.toLinear(); mapOffset < codeExtents.end.toLinear(); ++mapOffset) {
        const int m = visited.at(mapOffset);
        //debug(hexVal(mapOffset) + ": " + to_string(m));
        if (mapOffset % 16 == 0) {
            if (mapOffset != 0) mapFile << endl;
            mapFile << hex << setw(5) << setfill('0') << mapOffset << " ";
        }
        mapFile << dec << setw(5) << setfill(' ') << m << " ";
        if (m != prev) { // new chunk begin
            if (prev != NOT_VISITED) { // need to close previous routine and start a new one
                Block block{Address(blockStart), Address(mapOffset - 1)};
                debug(hexVal(mapOffset) + ": closing block " + block + " for routine " + r.name + ", entrypoint " + r.entrypoint());
                // this is the main body of the routine, update the extents
                if (block.contains(r.entrypoint())) { 
                    debug(hexVal(mapOffset) + ": main body for routine " + to_string(m)); 
                    r.extents.end = block.end;
                    // rebase extents to the beginning of code so its address is relative to the start of the load module
                    r.extents.rebase(codeExtents.begin.segment);
                }
                // otherwise this is a head or tail chunk of the routine, add to chunks
                else { 
                    debug(hexVal(mapOffset) + ": chunk for routine " + to_string(m)); 
                    block.rebase(codeExtents.begin.segment);
                    r.chunks.push_back(block); 
                }
            }
            // start new chunk
            if (m != NOT_VISITED) {
                debug(hexVal(mapOffset) + ": starting block for routine " + to_string(m));
                blockStart = mapOffset;
            }
        }
        prev = m;
    }
    // sort routines by entrypoint
    info("Successfully analyzed code, found " + to_string(routines.size()) + " routines");
    std::sort(routines.begin(), routines.end(), [](const Routine &a, const Routine &b){
        return a.entrypoint().toLinear() < b.entrypoint().toLinear();
    });
#endif    

    ret.success = true;
    return ret;
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