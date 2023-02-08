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
#include <regex>

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

bool Routine::isReachable(const Block &b) const {
    return std::find(reachable.begin(), reachable.end(), b) != reachable.end();
}

bool Routine::isUnreachable(const Block &b) const {
    return std::find(unreachable.begin(), unreachable.end(), b) != unreachable.end();
}

bool Routine::colides(const Block &block) const {
    for (const auto &b : reachable) {
        if (b.intersects(block)) {
            debug("Block "s + block.toString() + " colides with reachable block " + b.toString() + " of routine " + toString(false));
            return true;
        }
    }
    for (const auto &b : unreachable) {
        if (b.intersects(block)) {
            debug("Block "s + block.toString() + " colides with unreachable block " + b.toString() + " of routine " + toString(false));
            return true;
        }
    }    
    return false;
}

string Routine::toString(const bool showChunks) const {
    ostringstream str;
    str << extents.toString(true) << ": " << name;
    if (!showChunks) return str.str();

    auto blocks = sortedBlocks();
    for (const auto &b : blocks) {
        str << endl << "\t" << b.toString(true) << ": ";
        if (b.begin == entrypoint())
            str << "main";
        else if (isReachable(b))
            str << "chunk";
        else if (isUnreachable(b))
            str << "unreachable";
        else 
            str << "unknown";
    }
    return str.str();
}

std::vector<Block> Routine::sortedBlocks() const {
    vector<Block> blocks(reachable);
    copy(unreachable.begin(), unreachable.end(), back_inserter(blocks));
    sort(blocks.begin(), blocks.end());
    return blocks;
}

RoutineMap::RoutineMap(const SearchQueue &sq) {
    const Size routineCount = sq.routineCount();
    if (routineCount == 0)
        throw AnalysisError("Attempted to create routine map from search queue with no routines");
    info("Building routine map from search queue contents ("s + to_string(routineCount) + " routines)");
    routines = vector<Routine>{routineCount};
    // assign automatic names to routines
    for (Size i = 1; i <= routines.size(); ++i) {
        auto &r = routines.at(i - 1);
        if (!r.name.empty()) continue;
        r.name = "routine_"s + to_string(i);
    }

    prevId = curBlockId = prevBlockId = NULL_ROUTINE;
    Block b;
    for (Size mapOffset = 0; mapOffset < sq.codeSize(); ++mapOffset) {
        curId = sq.getRoutineId(mapOffset);
        // do nothing as long as the value doesn't change, unless we encounter the routine entrypoint in the middle of a block, in which case we force a block close
        if (curId == prevId && !sq.isEntrypoint(Address{mapOffset})) continue;
        // value in map changed (or forced block close because of encounterted entrypoint), new block begins
        closeBlock(b, mapOffset, sq); // close old block and attribute to a routine if possible
        // start new block
        debug(hexVal(mapOffset) + ": starting block for routine " + to_string(curId));
        b = Block(mapOffset);
        curBlockId = curId;
        // last thing to do is memorize current id as previous for discovering when needing to close again
        prevId = curId;
    }
    // close last block finishing on the last byte of the memory map
    closeBlock(b, sq.codeSize(), sq);

    // calculate routine extents
    for (auto &r : routines) {
        Address entrypoint = r.entrypoint();
        assert(entrypoint.isValid());
        // locate main block (the one starting on the entrypoint) and set it as the extents' initial value
        auto mainBlock = std::find_if(r.reachable.begin(), r.reachable.end(), [entrypoint](const Block &b){
            return b.begin == entrypoint;
        });
        if (mainBlock == r.reachable.end())
            throw AnalysisError("Unable to find main block for routine "s + r.toString(false));
        r.extents = *mainBlock;
        // coalesce remaining blocks with the extents if possible
        for (const auto &b : r.sortedBlocks()) {
            if (b.begin < r.extents.begin) continue; // ignore blocks that begin before the routine entrypoint
            r.extents.coalesce(b);
        }
        debug("Calculated routine extents: "s + r.toString(false));
    }
    sort();
}

RoutineMap::RoutineMap(const std::string &path) {
    static const regex LSTFILE_RE{".*\\.(lst|LST)"};
    const auto fstat = checkFile(path);
    if (!fstat.exists) throw ArgError("File does not exist: "s + path);
    smatch match;
    if (regex_match(path, match, LSTFILE_RE)) loadFromIdaFile(path);
    else loadFromMapFile(path);
    debug("Done, found "s + to_string(routines.size()) + " routines");
    sort();
}

// matches routines by extents only, limited use, mainly unit test for alignment with IDA
Size RoutineMap::match(const RoutineMap &other) const {
    Size matchCount = 0;
    for (const auto &r : routines) {
        bool routineMatch = false;
        for (const auto &ro : other.routines) {
            if (r.extents == ro.extents) {
                debug("Found routine match for "s + r.toString() + " with " + ro.toString(false));
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

// check if any of the extents or chunks of routines in the map colides (contains or intersects) with a block
Routine RoutineMap::colidesBlock(const Block &b) const {
    const auto &found = find_if(routines.begin(), routines.end(), [&b](const Routine &r){
        return r.colides(b);
    });
    if (found != routines.end()) return *found;
    else return {};
}

// utility function used when constructing from  an instance of SearchQueue
void RoutineMap::closeBlock(Block &b, const Offset off, const SearchQueue &sq) {
    if (!b.isValid()) 
        return;

    b.end = Address{off - 1};
    debug(hexVal(off) + ": closing block starting at " + b.begin.toString() + ", curId = " + to_string(curId) + ", prevId = " + to_string(prevId) 
        + ", curBlockId = " + to_string(curBlockId) + ", prevBlockId = " + to_string(prevBlockId));
    if (curBlockId != NULL_ROUTINE) { // block contains reachable code
        // get handle to matching routine
        assert(curBlockId - 1 < routines.size());
        Routine &r = routines.at(curBlockId - 1);
        debug("closed reachable block " + b.toString() + " for routine " + to_string(curBlockId));
        if (sq.isEntrypoint(b.begin)) { // this is the entrypoint block of the routine
            debug("block starts at routine entrypoint");
            assert(!r.extents.isValid());
            // set entrypoint of routine
            r.extents.begin = b.begin;
            if (b.begin == sq.startAddress()) r.name = "start";
        }
        // add reachable block
        r.reachable.push_back(b);
    }
    // block contains unreachable code or data, attribute to a routine if surrounded by that routine's blocks on both sides
    else if (prevBlockId != NULL_ROUTINE && curId == prevBlockId) { 
        // get handle to matching routine
        assert(prevBlockId - 1 < routines.size());
        Routine &r = routines.at(prevBlockId - 1);
        debug("closed unreachable block " + b.toString() + " for routine " + to_string(prevBlockId));
        r.unreachable.push_back(b);
    }
    else {
        assert(curBlockId == NULL_ROUTINE);
        debug("closed unclaimed block " + b.toString());
    }
    // remember the id of the block we just closed
    prevBlockId = curBlockId;
}

void RoutineMap::sort() {
    using std::sort;
    // sort routines by entrypoint
    std::sort(routines.begin(), routines.end(), [](const Routine &a, const Routine &b){
        return a.entrypoint() < b.entrypoint();
    });

    // sort chunks within routines by begin address
    for (auto &r : routines) {
        sort(r.reachable.begin(), r.reachable.end());
        sort(r.unreachable.begin(), r.unreachable.end());
    }
}

void RoutineMap::save(const std::string &path) const {
    if (empty()) return;
    info("Saving routine map to "s + path);
    ofstream file{path};
    for (const auto &r : routines) {
        file << r.name << ": e" << r.extents.toString(true, false);
        const auto blocks = r.sortedBlocks();
        for (const auto &b : blocks) {
            if (r.isReachable(b))
                file << " r" << b.toString(true, false);
            else
                file << " u" << b.toString(true, false);
        }
        file << endl;
    }
}

void RoutineMap::dump() const {
    if (empty()) {
        info("--- Empty routine map");
        return;
    }

    // display routines
    info("--- Routine map containing "s + to_string(routines.size()) + " routines");
    for (const auto &r : routines) {
        info(r.toString(true));
    }
}

void RoutineMap::loadFromMapFile(const std::string &path) {
    debug("Loading routine map from "s + path);
    ifstream mapFile{path};
    string line, token;
    Size lineno = 0;
    bool colideOk = true;
    while (safeGetline(mapFile, line) && colideOk) {
        lineno++;
        istringstream sstr{line};
        Routine r;
        while (sstr >> token) {
            if (token.empty()) continue;
            if (r.name.empty() && token.back() == ':') { // routine name
                r.name = token.substr(0, token.size() - 1);
            }
            else { // an extents, reachable
                // first character is block type
                char blockType = token.front(); 
                const Block block{token.substr(1, token.size() - 1)};
                // check block for collisions agains rest of routines already in the map as well as the currently built routine
                Routine colideRoutine = colidesBlock(block);
                if (!colideRoutine.isValid() && r.colides(block)) 
                    colideRoutine = r;
                if (colideRoutine.isValid())
                    throw AnalysisError("line "s + to_string(lineno) + ": block " + block.toString() + " colides with routine " + colideRoutine.toString(false));
                // add block to routine
                switch(blockType) {
                case 'e': 
                    r.extents = block; 
                    break;
                case 'r': 
                    r.reachable.push_back(block); 
                    break;
                case 'u': 
                    r.unreachable.push_back(block);
                    break;
                default:
                    throw AnalysisError("line "s + to_string(lineno) + ": unrecognized token: '"s + token + "'");
                }
            }  
        }
        if (r.extents.isValid()) {
            debug("routine: "s + r.toString());
            routines.push_back(r);
        }
    }
}

// create routine map from IDA .lst file
// TODO: add collision checks
void RoutineMap::loadFromIdaFile(const std::string &path) {
    debug("Loading IDA routine map from "s + path);
    ifstream fstr{path};
    string line, lastAddr;
    Size lineno = 1;
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
            debug("Closing routine "s + r.name + " @ " + r.extents.end.toString());
            endpAddr = Address(); // makes address invalid
        }
        // start new routine
        if (token[2] == "proc") { 
            routines.emplace_back(Routine{token[1], curAddr});
            const auto &r = routines.back();
            debug("Found start of routine "s + r.name + " @ " + r.extents.begin.toString());
        }
        // routine end, but IDA places endp at the offset of the beginning of the last instruction, 
        // and we want the end to include the last instructions' bytes, so just memorize the address and close the routine later
        else if (token[2] == "endp") { 
            endpAddr = curAddr;
            debug("Found end of routine @ " + endpAddr.toString());
        }
        lineno++;
    }
}

RegisterState::RegisterState() {
    for (int i = REG_AL; i <= REG_FLAGS; ++i) {
        Register r = (Register)i;
        regs_.set(r, 0);
        known_.set(r, 0);
    }
}

bool RegisterState::isKnown(const Register r) const {
    if (regIsWord(r)) return known_.get(r) == WORD_KNOWN;
    else return known_.get(r) == BYTE_KNOWN;
}

Word RegisterState::getValue(const Register r) const {
    if (isKnown(r)) return regs_.get(r);
    else return 0;
}

void RegisterState::setValue(const Register r, const Word value) {
    setState(r, value, true);
}

void RegisterState::setUnknown(const Register r) {
    setState(r, 0, false);
}

string RegisterState::stateString(const Register r) const {
    if (regIsWord(r))
        return (isKnown(r) ? hexVal(regs_.get(r), false, true) : "????");
    else
        return (isKnown(r) ? hexVal(static_cast<Byte>(regs_.get(r)), false, true) : "??");
}

string RegisterState::regString(const Register r) const {
    string ret = regName(r) + " = ";
    if (regIsGeneral(r)) {
        if (isKnown(r)) ret += hexVal(regs_.get(r), false, true);
        else {
            ret += stateString(regHigh(r));
            ret += stateString(regLow(r));
        }
    }
    else ret += stateString(r);
    return ret;
}

string RegisterState::toString() const {
    ostringstream str;
    str << std::hex 
        << regString(REG_AX) << ", " << regString(REG_BX) << ", "
        << regString(REG_CX) << ", " << regString(REG_DX) << endl
        << regString(REG_SI) << ", " << regString(REG_DI) << ", "
        << regString(REG_BP) << ", " << regString(REG_SP) << endl
        << regString(REG_CS) << ", " << regString(REG_DS) << ", "
        << regString(REG_SS) << ", " << regString(REG_ES) << endl
        << regString(REG_IP) << ", " << regString(REG_FLAGS);
    return str.str();
}  

void RegisterState::setState(const Register r, const Word value, const bool known) {
    if (regIsWord(r)) {
        regs_.set(r, value);
        known_.set(r, known ? WORD_KNOWN : 0);
    }
    else {
        regs_.set(r, value);
        known_.set(r, known ? BYTE_KNOWN : 0);
    }
}

string SearchPoint::toString() const {
    ostringstream str;
    str << "[" << address.toString() << " / " << routineId << " / " << (isCall ? "call" : "jump") << "]";
    return str.str();
}

SearchQueue::SearchQueue(const Size codeSize, const SearchPoint &seed) : visited(codeSize, NULL_ROUTINE) {
    queue.push_front(seed);
    start = seed.address;
}

string SearchQueue::statusString() const { 
    return "[r"s + to_string(curSearch.routineId) + "/q" + to_string(size()) + "]"; 
} 

int SearchQueue::getRoutineId(const Offset off) const { 
    assert(off < visited.size());
    return visited.at(off); 
}

void SearchQueue::markRoutine(const Offset off, const Size length, int id) {
    if (id == NULL_ROUTINE) id = curSearch.routineId;
    fill(visited.begin() + off, visited.begin() + off + length, id);
}

SearchPoint SearchQueue::nextPoint() {
    if (!empty()) {
        curSearch = queue.front();
        queue.pop_front();
    }
    return curSearch;
}

bool SearchQueue::hasPoint(const Address &dest, const bool call) const {
    SearchPoint findme(dest, 0, call, RegisterState());
    const auto &it = std::find_if(queue.begin(), queue.end(), [&](const SearchPoint &p){
        return p.match(findme);
    });
    return it != queue.end();
};

bool SearchQueue::isEntrypoint(const Address &addr) const {
    return std::find(entrypoints.begin(), entrypoints.end(), addr) != entrypoints.end();
}

// function call, create new routine at destination if either not visited, 
// or visited but destination was not yet discovered as a routine entrypoint and this call now takes precedence and will replace it
void SearchQueue::saveCall(const Address &dest, const RegisterState &regs) {
    const int destId = getRoutineId(dest.toLinear());
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
        queue.emplace_front(SearchPoint(dest, newRoutineId, true, regs));
        entrypoints.push_back(dest);
    }
}

// conditional jump, save as destination to be investigated, belonging to current routine
void SearchQueue::saveJump(const Address &dest, const RegisterState &regs) {
    const int destId = getRoutineId(dest.toLinear());
    if (destId != NULL_ROUTINE) 
        debug("Jump destination already claimed by routine "s + to_string(destId));
    else if (hasPoint(dest, false))
        debug("Search queue already contains jump to address "s + dest.toString());
    else { // not claimed by any routine and not yet in queue
        debug("jump destination not belonging to any routine "s + dest.toString() + ", claiming for existing routine " + to_string(curSearch.routineId));
        queue.emplace_front(SearchPoint(dest, curSearch.routineId, false, regs));
    }
}

void SearchQueue::dumpVisited(const string &path) const {
    // dump map to file for debugging
    ofstream mapFile(path);
    mapFile << "      ";
    for (int i = 0; i < 16; ++i) mapFile << hex << setw(5) << setfill(' ') << i << " ";
    mapFile << endl;
    for (size_t mapOffset = 0; mapOffset < visited.size(); ++mapOffset) {
        const int id = getRoutineId(mapOffset);
        //debug(hexVal(mapOffset) + ": " + to_string(m));
        if (mapOffset % 16 == 0) {
            if (mapOffset != 0) mapFile << endl;
            mapFile << hex << setw(5) << setfill('0') << mapOffset << " ";
        }
        mapFile << dec << setw(5) << setfill(' ') << id << " ";
    }
}

Executable::Executable(const MzImage &mz) : entrypoint(mz.entrypoint()), stack(mz.stackPointer()), reloc(mz.loadSegment())  {
    const Byte *data = mz.loadModuleData();
    copy(data, data + mz.loadModuleSize(), std::back_inserter(code));
}

// explore the code without actually executing instructions, discover routine boundaries
// TODO: support multiple code segments
// TODO: identify routines through signatures generated from OMF libraries
// TODO: trace usage of bp register (sub/add) to determine stack frame size of routines
// TODO: store references to potential jump tables (e.g. jmp cs:[bx+0xc08]), if unclaimed after initial search, try treating entries as pointers and run second search before coalescing blocks?
RoutineMap Executable::findRoutines() {
    const Byte *codeData = code.data();
    const Size codeSize = code.size();
    const Block codeExtents = Block({entrypoint.segment, 0}, Address(SEG_OFFSET(entrypoint.segment) + codeSize));
    RegisterState initRegs;
    initRegs.setValue(REG_CS, entrypoint.segment);
    initRegs.setValue(REG_IP, entrypoint.offset);
    initRegs.setValue(REG_SS, stack.segment);
    initRegs.setValue(REG_SP, stack.offset);
    debug("initial register values:\n"s + initRegs.toString());
    // queue for BFS search
    SearchQueue searchQ(codeSize, SearchPoint(entrypoint, 1, true, initRegs));

    auto searchMessage = [](const Address &a, const string &msg) {
        output(a.toString() + ": " + msg, LOG_ANALYSIS, LOG_DEBUG);
    };

    info("Analyzing code in range " + codeExtents);
    // iterate over entries in the search queue
    while (!searchQ.empty()) {
        // get a location from the queue and jump to it
        const SearchPoint search = searchQ.nextPoint();
        Address csip = search.address;
        const int rid = searchQ.getRoutineId(csip.toLinear());
        searchMessage(csip, "starting search at new location, call: "s + to_string(search.isCall) + ", queue: " + searchQ.statusString());
        if (rid != NULL_ROUTINE && !search.isCall) {
            debug("location already claimed by routine "s + to_string(rid) + " and search point did not originate from a call, skipping");
            continue;
        }
        RegisterState regs = search.regs;
        bool scan = true;
        // iterate over instructions at current search location in a linear fashion, until an unconditional jump or return is encountered
        while (scan) {
            if (!codeExtents.contains(csip))
                throw AnalysisError("Advanced past loaded code extents: "s + csip.toString());
            const Offset instrOffs = csip.toLinear();
            Instruction i(csip, codeData + instrOffs);
            regs.setValue(REG_CS, csip.segment);
            regs.setValue(REG_IP, csip.offset);
            Address jumpAddr = csip;
            bool call = false;
            Word memWord;
            Byte memByte;
            vector<Register> touchedRegs;
            // mark memory map items corresponding to the current instruction as belonging to the current routine
            searchQ.markRoutine(instrOffs, i.length);
            // interpret the instruction
            switch (i.iclass) {
            // jumps
            case INS_JMP:
                // conditional jump, put destination into search queue to check out later
                if (opcodeIsConditionalJump(i.opcode)) {
                    jumpAddr.offset = i.relativeOffset();
                    searchMessage(csip, "encountered conditional near jump to "s + jumpAddr.toString());
                }
                else switch (i.opcode) {
                // unconditional jumps, cannot continue at current location, need to jump now
                case OP_JMP_Jb: 
                case OP_JMP_Jv: 
                    jumpAddr.offset = i.relativeOffset();
                    searchMessage(csip, "encountered unconditional near jump to "s + jumpAddr.toString());
                    scan = false;
                    break;
                case OP_GRP5_Ev:
                    searchMessage(csip, "unknown near jump target: "s + i.toString());
                    break; 
                default: 
                    throw AnalysisError("Unsupported jump opcode: "s + hexVal(i.opcode));
                }
                break;
            case INS_JMP_FAR:
                if (i.op1.type == OPR_IMM32) {
                    jumpAddr = Address{i.op1.immval.u32}; 
                    searchMessage(csip, "encountered unconditional far jump to "s + jumpAddr.toString());
                    scan = false;
                }
                else searchMessage(csip, "unknown far jump target: "s + i.toString());
                break;
            // loops
            case INS_LOOP:
            case INS_LOOPNZ:
            case INS_LOOPZ:
                jumpAddr.offset = i.relativeOffset();
                searchMessage(csip, "encountered loop to "s + jumpAddr.toString());
                break;
            // calls
            case INS_CALL:
                if (i.op1.type == OPR_IMM16) {
                    jumpAddr.offset = i.relativeOffset();
                    searchMessage(csip, "encountered near call to "s + jumpAddr.toString());
                    call = true;
                }
                else if (operandIsReg(i.op1.type) && regs.isKnown(i.op1.regId())) {
                    jumpAddr.offset = regs.getValue(i.op1.regId());
                    searchMessage(csip, "encountered near call through register to "s + jumpAddr.toString());
                    call = true;
                }
                else if (operandIsMemImmediate(i.op1.type) && regs.isKnown(REG_DS)) {
                    Address memAddr{regs.getValue(REG_DS), i.op1.immval.u16};
                    if (codeExtents.contains(memAddr)) {
                        searchMessage(csip, "encountered near call through mem pointer to "s + jumpAddr.toString());
                        memcpy(&memWord, codeData + memAddr.toLinear(), sizeof(Word));
                        jumpAddr.offset = memWord;
                        call = true;
                    }
                    else debug("call destination address outside code extents: " + memAddr.toString());
                }
                else searchMessage(csip, "unknown near call target: "s + i.toString());
                break;
            case INS_CALL_FAR:
                if (i.op1.type == OPR_IMM32) {
                    jumpAddr = Address(i.op1.immval.u32);
                    searchMessage(csip, "encountered far call to "s + jumpAddr.toString());
                    call = true;
                }
                else searchMessage(csip, "unknown far call target: "s + i.toString());
                break;
            // returns
            case INS_RET:
            case INS_RETF:
            case INS_IRET:
                searchMessage(csip, "returning from routine");
                scan = false;
                break;
            // track some movs to be able to infer jump/call destinations from register values
            // TODO: use a Cpu instance, evaluate arithmetic instructions to harvest more reg values than only from mov
            case INS_MOV:
                if (operandIsReg(i.op1.type)) {
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
                    else if (operandIsMemImmediate(i.op2.type) && regs.isKnown(prefixRegId(i.prefix))) {
                        Address memAddr(regs.getValue(prefixRegId(i.prefix)), i.op2.immval.u16);
                        if (codeExtents.contains(memAddr)) switch(i.op2.size) {
                        case OPRSZ_BYTE:
                            assert(memAddr.toLinear() < codeSize);
                            memByte = codeData[memAddr.toLinear()];
                            regs.setValue(dest, memByte); 
                            set = true;
                            break;
                        case OPRSZ_WORD:
                            assert(memAddr.toLinear() < codeSize);
                            memcpy(&memWord, codeData + memAddr.toLinear(), sizeof(Word));
                            regs.setValue(dest, memWord);
                            set = true;
                            break;
                        }
                        else searchMessage(csip, "mov source address outside code extents: " + memAddr.toString());
                    }
                    if (set) {
                        searchMessage(csip, "encountered move to register: "s + i.toString());
                        debug(regs.toString());
                    }
                }
                break;
            default:
                touchedRegs = i.touchedRegs();
                if (!touchedRegs.empty()) {
                    for (Register r : touchedRegs) {
                        regs.setUnknown(r);
                    }
                }
            } // switch on instruction class

            // if the processed instruction was a jump or a call, store its destination in the search queue
            if (jumpAddr != csip) {
                if (codeExtents.contains(jumpAddr)) {
                    if (call) searchQ.saveCall(jumpAddr, regs);
                    else searchQ.saveJump(jumpAddr, regs);
                }
                else searchMessage(csip, "search point destination outside code boundaries: "s + jumpAddr.toString());
            } 
            // advance to next instruction
            csip.offset += i.length;
        } // iterate over instructions
    } // next address from callstack
    info("Done analyzing code");

    // iterate over discovered memory map and create routine map
    return RoutineMap{searchQ};
}

struct RoutinePair {
    Routine r1, r2;
    RoutinePair(const std::string &name, const Address &e1, const Address &e2) : r1{name, e1}, r2{name, e2} {}
    std::string toString() const {
        ostringstream str;
        str << "'" << r1.name << ", entrypoints @ " << r1.entrypoint() << " <-> " << r2.entrypoint();
        return str.str();
    }
};

bool Executable::compareCode(const RoutineMap &map, const Executable &other) const {
    if (map.empty()) {
        throw AnalysisError("Unable to compare executables, routine map is empty");
    }
    const Byte 
        *refCode = code.data(),
        *cmpCode = other.code.data();
    const Size 
        refCodeSize = code.size(),
        cmpCodeSize = other.code.size();
//    const Block refCodeExtents = Block({entrypoint.segment, 0}, Address(SEG_OFFSET(entrypoint.segment) + codeSize));

  //  for (Size rid = 1)
    return true;
}