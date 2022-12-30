#include "dos/analysis.h"
#include "dos/opcodes.h"
#include "dos/instruction.h"
#include "dos/output.h"
#include "dos/util.h"

#include <iostream>
#include <sstream>
#include <stack>
#include <algorithm>
#include <fstream>

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

void RoutineMap::dump() const {
    // display routines
    for (const auto &r : routines) {
        output(r.toString(), LOG_ANALYSIS, LOG_INFO);
    }
}

// explore the code without actually executing instructions, discover routine boundaries
// TODO: trace modifications to CS, support multiple code segments
// TODO: process GRP5 jumps and calls
// TODO: trace other register values for discovering register-dependent calls?
RoutineMap analyze(const Byte *code, const Size size, const Address entrypoint) {
    // memory map for marking which locations belong to which routines, value of 0 is undiscovered
    const int UNDISCOVERED = 0;
    vector<int> memoryMap(size, UNDISCOVERED);
    // store for all routines found
    RoutineMap ret;
    vector<Routine> &routines = ret.routines;
    // stack structure for DFS search
    stack<Frame> callstack;
    // create default routine for entrypoint
    routines.push_back(Routine("start", 1, entrypoint));
    Routine *curRoutine = &(routines.back());
    callstack.push(Frame(entrypoint, 1));
    Address csip{entrypoint};
    const Block codeExtents = Block({entrypoint.segment, 0}, Address(SEG_OFFSET(entrypoint.segment) + size));

    auto analysisMessage = [&](const string &str, const LogPriority pri = LOG_VERBOSE) {
        output("["s + to_string(callstack.size()) + "]: " + csip + ": " + str, LOG_ANALYSIS, pri);
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
    auto checkAndJump = [&](const Address &destination, const string &type, const size_t instrLen = 0, const bool call = false) {
        if (codeExtents.contains(destination)) {
            debug("taking " + type + " to " + destination);
            if (instrLen) callstack.push(Frame(csip + instrLen, curRoutine->id));
            if (call) switchRoutine(destination);
            csip = destination;
            return true;
        }
        else {
            debug(type + " to " + destination + " outside loaded code, ignoring");
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
        debug("now in routine '" + curRoutine->name + "', id: " + to_string(curRoutine->id));
        bool done = false;
        while (!done) {
            if (!codeExtents.contains(csip)) {
                error("ERROR: advanced past loaded code extents");
                return {};
            }
            const Offset instrOffs = csip.toLinear();
            Instruction i{code + instrOffs};
            // mark memory map locations as belonging to the current routine
            const Size instrLen = i.length;
            auto instrIt = memoryMap.begin() + instrOffs;
            const int curId = memoryMap[instrOffs];
            // cpuMessage(regs_.csip().toString() +  ": opcode " + hexVal(opcode_) + ", instrLen = " + to_string(instrLen));
            // make sure this location isn't discovered yet
            if (curId != UNDISCOVERED) {
                debug("location already belongs to routine " + to_string(curId));
                break;
            }
            std::fill(instrIt, instrIt + instrLen, curRoutine->id);
            //cpuMessage(disasm());
            switch (i.iclass)
            {
            case INS_JMP:
                switch (i.opcode) {
                // unconditional jumps: don't store current location on stack, we aren't coming back
                case OP_JMP_Jb:
                case OP_JMP_Jv:
                    if (checkAndJump(csip + instrLen + i.op1.immval, "unconditional jump")) continue;
                    else break;
                case OP_JMP_Ap:
                    if (checkAndJump(Address{i.op1.immval}, "unconditional far jump")) continue;
                    else break;
                // conditional jumps, store  next instruction's address on callstack to come back to
                default:
                    if (checkAndJump(csip + instrLen + i.op1.immval, "conditional jump", instrLen)) continue;
                    else break;
                }
                break;
            case INS_JMP_FAR:
                if (checkAndJump(Address{i.op1.immval}, "unconditional far jump")) continue;
                else break;            
            case INS_JCXZ: // TODO: group together with other conditionals
                if (checkAndJump(csip + instrLen + i.op1.immval, "jcxz jump", instrLen)) continue;
                else break;                
            case INS_LOOP:
            case INS_LOOPNZ:
            case INS_LOOPZ:
                if (checkAndJump(csip + instrLen + i.op1.immval, "loop", instrLen)) continue;
                else break;
            // calls
            case INS_CALL:
                if (checkAndJump(csip + instrLen + i.op1.immval, "call", instrLen, true)) continue;
                else break;
            case INS_CALL_FAR:
                if (checkAndJump(Address{i.op1.immval}, "far call", instrLen, true)) continue;
                else break;
            // return
            case INS_RET:
            case INS_RETF:
            case INS_IRET:
                debug("returning from " + curRoutine->name);
                done = true;
                break;
            // jumps and calls encoded with group opcode
            } // switch on instrucion type
            // advance to next instruction
            if (instrLen == 0) {
                error("calculated instruction length is zero, opcode " + hexVal(i.opcode));
                dumpStack();
                return {};
            }
            csip = csip + instrLen;
            // move to next instruction
        } // iterate over instructions
    } // next address from callstack
    // dump map to file for debugging, if >255 routines this will overflow
    ofstream mapFile("analysis.map", ios::binary);
    int prev = UNDISCOVERED;
    Offset chunkStart = 0;
    // iterate over memory map that we built up, identify contiguous chunks belonging to routines
    for (size_t mapOffset = codeExtents.begin.toLinear(); mapOffset < codeExtents.end.toLinear(); ++mapOffset) {
        const int m = memoryMap[mapOffset];
        debug(hexVal(mapOffset) + ": " + to_string(m));
        mapFile.put(static_cast<char>(m));
        if (m != prev) { // new chunk begin
            if (prev != UNDISCOVERED) { // attribute previous chunk to correct routine
                Routine &r = routines[prev - 1];
                Block chunk{Address(chunkStart), Address(mapOffset - 1)};
                debug("\tclosing chunk " + chunk + " for routine " + r.name + " @ " + r.entrypoint());
                // this is the main body of the routine, update the extents
                if (chunk.contains(r.entrypoint())) { 
                    debug("\tmain body"); 
                    r.extents.end = chunk.end;
                    // rebase extents to the beginning of code so its address is relative to the start of the load module
                    r.extents.rebase(codeExtents.begin.segment);
                }
                // otherwise this is a head or tail chunk of the routine, add to chunks
                else { 
                    debug("\tchunk"); 
                    chunk.rebase(codeExtents.begin.segment);
                    r.chunks.push_back(chunk); 
                }
            }
            // start new chunk
            if (m != UNDISCOVERED) {
                debug("\tstarting chunk");
                chunkStart = mapOffset;
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


RoutineMap findRoutines(const Byte *code, const Size size, const Address entrypoint) {
    return analyze(code, size, entrypoint);
}