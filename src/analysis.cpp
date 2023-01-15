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
RoutineMap::RoutineMap(const std::string &path) {
    const auto fstat = checkFile(path);
    if (!fstat.exists) throw ArgError("File does not exist: "s + path);
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

// explore the code without actually executing instructions, discover routine boundaries
// TODO: trace modifications to CS, support multiple code segments
// TODO: trace other register values for discovering register-dependent calls?
// TODO: identify routines through signatures generated from OMF libraries
RoutineMap findRoutines(const Executable &exe) {
    const Byte *code = exe.code.data();
    const Size size = exe.code.size();
    const Address entrypoint = exe.entrypoint;
    const Word baseSegment = exe.reloc;
    // memory map for marking which locations belong to which routines, value of 0 is undiscovered
    const int UNDISCOVERED = 0;
    vector<int> memoryMap(size, UNDISCOVERED); // TODO: store addresses from loaded exe in map, otherwise they don't match after analysis done
    // store for all routines found
    RoutineMap ret;
    vector<Routine> &routines = ret.routines;
    // stack structure for DFS search
    stack<Frame> callstack;
    // create default routine for entrypoint
    routines.push_back(Routine("start", 1, entrypoint));
    Routine *curRoutine = &(routines.back());
    callstack.push(Frame(entrypoint, 1));
    Address csip = entrypoint;
    const Block codeExtents = Block({entrypoint.segment, 0}, Address(SEG_OFFSET(entrypoint.segment) + size));

    auto analysisMessage = [&](const string &str, const LogPriority pri = LOG_VERBOSE) {
        output("[r"s + to_string(curRoutine->id) + "/s" + to_string(callstack.size()) + "]: " + Address(csip.segment + baseSegment, csip.offset).toString() + ": " + str, LOG_ANALYSIS, pri);
    };

    // create new routine and switch to it, or switch to one that exists already
    auto switchRoutine = [&](const Address &entrypoint) {
        auto existingRoutine = std::find_if(routines.begin(), routines.end(), [&entrypoint](const Routine &r){
            return r.entrypoint() == entrypoint;
        });
        if (existingRoutine == routines.end()) {
            const int id = routines.back().id + 1;
            const string name = "routine_" + to_string(id);
            routines.push_back(Routine(name, id, entrypoint));
            curRoutine = &(routines.back());
            debug("\tcreated routine " + curRoutine->name + " at entrypoint " + curRoutine->entrypoint());
        }
        else {
            curRoutine = &(*existingRoutine);
            debug("\texisting routine " + curRoutine->name + " at entrypoint " + curRoutine->entrypoint());
        }
    };

    // check if jump/call destination is within current code extents and jump to it, 
    // optionally placing a return location on the callstack and switching to a different routine
    auto checkAndJump = [&](const Address &destination, const string &type, const Word instrLen = 0, const bool call = false) {
        if (codeExtents.contains(destination)) {
            analysisMessage("taking " + type + " to " + Address(destination.segment + baseSegment, destination.offset).toString());
            if (instrLen) callstack.push(Frame(Address(csip, instrLen), curRoutine->id));
            if (call) switchRoutine(destination);
            csip = destination;
            return true;
        }
        // XXX: check if jump target lies before current routine's entrypoint, in such case don't go there,
        // otherwise that routine will have locations attributed to it which do not show in the output map
        else {
            analysisMessage(type + " to " + destination + " outside loaded code, ignoring");
            return false;
        }
    };
    
    auto dumpStack = [&]() {
        debug("Current call stack:");
        while (!callstack.empty()) {
            const Frame f = callstack.top();
            debug("["s + to_string(callstack.size()) + "]: id " + to_string(f.id) + ", address " + f.address);
            callstack.pop();
        }
    };

    info("Analyzing code in range " + codeExtents);
    while (!callstack.empty()) {
        // pop an address of the stack and jump to it
        const Frame frame = callstack.top();
        curRoutine = &(routines[frame.id - 1]);
        callstack.pop();
        csip = frame.address;
        analysisMessage("now in routine '" + curRoutine->name + "', id: " + to_string(curRoutine->id));
        bool done = false;
        while (!done) {
            if (!codeExtents.contains(csip)) {
                analysisMessage("ERROR: advanced past loaded code extents", LOG_ERROR);
                return {};
            }
            const Offset instrOffs = csip.toLinear();
            Instruction i{code + instrOffs};
            //analysisMessage("Instruction at offset "s + hexVal(instrOffs) + ": " + i.toString());
            // mark memory map locations as belonging to the current routine
            const Byte instrLen = i.length;
            auto instrIt = memoryMap.begin() + instrOffs;
            const int curId = memoryMap[instrOffs];
            // cpuMessage(regs_.csip().toString() +  ": opcode " + hexVal(opcode_) + ", instrLen = " + to_string(instrLen));
            // make sure this location isn't discovered yet
            if (curId != UNDISCOVERED) {
                // XXX: remove routine if empty extents, here or during sorting
                analysisMessage("location already belongs to routine " + to_string(curId));
                break;
            }
            // XXX: ida puts the procedure end on the instruction start, match convention to get more compatibility?
            std::fill(instrIt, instrIt + instrLen, curRoutine->id);
            switch (i.iclass)
            {
            case INS_JMP:
            {
                Address jumpDest(csip, instrLen);
                string jumpType;
                Word jumpReturn = 0;
                bool jumpOk = true;
                switch (i.opcode) {
                // unconditional jumps: don't store current location on stack, we aren't coming back
                case OP_JMP_Jb: jumpDest += i.op1.imm.s8; jumpType = "unconditional 8bit jump"; break;
                case OP_JMP_Jv: jumpDest += i.op1.imm.s16; jumpType = "unconditional 16bit jump"; break;
                case OP_JMP_Ap: jumpDest = Address{i.op1.imm.u32}; jumpType = "unconditional far jump"; break;
                // conditional 8bit jumps, store  next instruction's address on callstack to come back to
                case OP_GRP5_Ev:
                    analysisMessage("unknown near jump target: "s + i.toString());
                    jumpOk = false;
                    break; 
                default: jumpDest += i.op1.imm.s8; jumpType = "conditional 8bit jump"; jumpReturn = instrLen; break;
                }
                if (jumpOk && checkAndJump(jumpDest, jumpType, jumpReturn)) continue; // if jump allowed, continue to next loop iteration - move to frame from callstack top
            }
                break; // otherwise, break out from switch and advance to next instruction
            case INS_JMP_FAR:
                if (i.op1.type == OPR_IMM32) {
                    if (checkAndJump(Address{i.op1.imm.u32}, "unconditional far jump")) continue;
                }
                else analysisMessage("unknown far jump target: "s + i.toString());
                break;
            case INS_JCXZ: // TODO: group together with other conditionals
                if (checkAndJump(Address(csip, instrLen + i.op1.imm.s8), "jcxz jump", instrLen)) continue;
                break;                
            case INS_LOOP:
            case INS_LOOPNZ:
            case INS_LOOPZ:
                if (checkAndJump(Address(csip, instrLen + i.op1.imm.s8), "loop", instrLen)) continue;
                break;
            // calls
            case INS_CALL:
                if (i.op1.type == OPR_IMM16) {
                    if (checkAndJump(Address(csip, instrLen + i.op1.imm.u16), "call", instrLen, true)) continue;
                }
                else analysisMessage("unknown near call target: "s + i.toString());
                break;
            case INS_CALL_FAR:
                if (i.op1.type == OPR_IMM32) {
                    if (checkAndJump(Address{i.op1.imm.u32}, "far call", instrLen, true)) continue;
                }
                else analysisMessage("unknown far call target: "s + i.toString());
                break;
            // return
            case INS_RET:
            case INS_RETF:
            case INS_IRET:
                analysisMessage("returning from " + curRoutine->name);
                done = true;
                break;
            // jumps and calls encoded with group opcode
            } // switch on instrucion type

            // advance to next instruction
            if (instrLen == 0) {
                analysisMessage("ERROR: calculated instruction length is zero, opcode " + hexVal(i.opcode), LOG_ERROR);
                dumpStack();
                return {};
            }
            csip += static_cast<SByte>(instrLen);
        } // iterate over instructions
    } // next address from callstack
    analysisMessage("Done analyzing code");

    // iterate over memory map that we built up, identify contiguous chunks belonging to routines
    // dump map to file for debugging
    ofstream mapFile("analysis.map");
    mapFile << "      ";
    for (int i = 0; i < 16; ++i) mapFile << hex << setw(5) << setfill(' ') << i << " ";
    mapFile << endl;
    int prev = UNDISCOVERED, undiscoveredPrev = UNDISCOVERED;
    Offset blockStart = 0;
    Block undiscoveredBlock;
    // find runs of undiscovered bytes surrounded by bytes discovered as belonging to the same routine and coalesce them with the routine
    // XXX: those areas are still practically undiscovered as we did not pass over their instructions, so we are missing paths leading from there,
    //      perhaps need a second analysis pass?
    for (size_t mapOffset = codeExtents.begin.toLinear(); mapOffset < codeExtents.end.toLinear(); ++mapOffset) {
        const int m = memoryMap[mapOffset];
        if (m == UNDISCOVERED && (prev != UNDISCOVERED || mapOffset == 0))  {
            undiscoveredBlock = Block{Address{mapOffset}}; // start new undiscovered block
            undiscoveredPrev = prev; // store id of routine preceeding the new block
        }
        else if (m != UNDISCOVERED && undiscoveredPrev == m) { // end of undiscovered block and current routine matches the one before the block
            undiscoveredBlock.end = Address{mapOffset - 1};
            // overwrite undiscovered block with id of routine surrounding it
            fill(memoryMap.begin() + undiscoveredBlock.begin.toLinear(), memoryMap.begin() + undiscoveredBlock.end.toLinear(), m);
        }
        prev = m;
    }
    for (size_t mapOffset = codeExtents.begin.toLinear(); mapOffset < codeExtents.end.toLinear(); ++mapOffset) {
        const int m = memoryMap[mapOffset];
        //debug(hexVal(mapOffset) + ": " + to_string(m));
        if (mapOffset % 16 == 0) {
            if (mapOffset != 0) mapFile << endl;
            mapFile << hex << setw(5) << setfill('0') << mapOffset << " ";
        }
        mapFile << dec << setw(5) << setfill(' ') << m << " ";
        if (m != prev) { // new chunk begin
            if (prev != UNDISCOVERED) { // need to close previous routine and start a new one
                Routine &r = routines[prev - 1];
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
            if (m != UNDISCOVERED) {
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

    ret.success = true;
    return ret;
}

bool compareCode(const Executable &base, const Executable &object) {
    const Byte 
        *bCode = base.code.data(),
        *oCode = object.code.data();
    const Size
        bSize = base.code.size(),
        oSize = object.code.size();
    Offset
        bOffset = base.entrypoint.toLinear(),
        oOffset = object.entrypoint.toLinear();

    // keep comparing instructions between the two executables (base and object) until a mismatch is found
    bool done = false;
    while (!done) {
        if (bOffset >= bSize) {
            error("Passed base code boundary: "s + hexVal(bOffset));
            return false;
        }
        if (oOffset >= oSize) {
            error("Passed object code boundary: "s + hexVal(oOffset));
            return false;
        }

        const Byte 
            *bCur = bCode + bOffset,
            *oCur = oCode + oOffset;
        Instruction bi{bCur}, oi{oCur};
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

        // move to next instruction pair
        bOffset += bi.length;
        oOffset += oi.length;
    }

    return true;
}