#include "dos/analysis.h"
#include "dos/opcodes.h"
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
#include <map>

using namespace std;

static void debug(const string &msg) {
    output(msg, LOG_ANALYSIS, LOG_DEBUG);
}

static void verbose(const string &msg) {
    output(msg, LOG_ANALYSIS, LOG_VERBOSE);
}

static void info(const string &msg) {
    output(msg, LOG_ANALYSIS, LOG_INFO);
}

static void error(const string &msg) {
    output("ERROR: "s + msg, LOG_ANALYSIS, LOG_ERROR);
}

bool Routine::isReachable(const Block &b) const {
    return std::find(reachable.begin(), reachable.end(), b) != reachable.end();
}

bool Routine::isUnreachable(const Block &b) const {
    return std::find(unreachable.begin(), unreachable.end(), b) != unreachable.end();
}

Block Routine::mainBlock() const {
    const Address ep = entrypoint();
    const auto &found = std::find_if(reachable.begin(), reachable.end(), [&ep](const Block &b){
        return b.begin == ep;
    });
    if (found != reachable.end()) return *found;
    return {};
}

Block Routine::blockContaining(const Address &a) const {
    const auto &found = std::find_if(reachable.begin(), reachable.end(), [&a](const Block &b){
        return b.contains(a);
    });
    if (found != reachable.end()) return *found;
    return {};
}

bool Routine::colides(const Block &block, const bool checkExtents) const {
    if (checkExtents && extents.intersects(block)) {
        debug("Block "s + block.toString() + " colides with extents of routine " + toString(false));
        return true;
    }    
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
    if (!showChunks || isUnchunked()) return str.str();

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
    // XXX: debugging purpose only, remove later
    sq.dumpVisited("search_queue.dump");
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
    Block b(0);
    for (Size mapOffset = 0; mapOffset < sq.codeSize(); ++mapOffset) {
        curId = sq.getRoutineId(mapOffset);
        // do nothing as long as the value doesn't change, unless we encounter the routine entrypoint in the middle of a block, in which case we force a block close
        if (curId == prevId && !sq.isEntrypoint(Address{mapOffset})) continue;
        // value in map changed (or forced block close because of encounterted entrypoint), new block begins
        closeBlock(b, mapOffset, sq); // close old block and attribute to a routine if possible
        // start new block
        debug(hexVal(mapOffset) + ": starting block for routine_" + to_string(curId));
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

Routine RoutineMap::getRoutine(const Address &addr) const {
    for (const Routine &r : routines)
        for (const Block &b : r.reachable)
            if (b.contains(addr)) return r;
    
    return {};
}

// matches routines by extents only, limited use, mainly unit test for alignment with IDA
Size RoutineMap::match(const RoutineMap &other) const {
    Size matchCount = 0;
    for (const auto &r : routines) {
        bool routineMatch = false;
        for (const auto &ro : other.routines) {
            if (r.extents == ro.extents) {
                debug("Found routine match for "s + r.toString(false) + " with " + ro.toString(false));
                routineMatch = true;
                matchCount++;
                break;
            }
        }
        if (!routineMatch) {
            debug("Unable to find match for "s + r.toString(false));
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
        debug("closed reachable block " + b.toString() + " for routine_" + to_string(curBlockId));
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
        debug("closed unreachable block " + b.toString() + " for routine_" + to_string(prevBlockId));
        r.unreachable.push_back(b);
    }
    else {
        assert(curBlockId == NULL_ROUTINE);
        debug("closed unclaimed block " + b.toString());
        unclaimed.push_back(b);
    }
    // remember the id of the block we just closed
    prevBlockId = curBlockId;
}

void RoutineMap::sort() {
    using std::sort;
    // sort routines by entrypoint
    std::sort(routines.begin(), routines.end());
    // sort unclaimed blocks by block start
    std::sort(unclaimed.begin(), unclaimed.end());
    // sort blocks within routines by block start
    for (auto &r : routines) {
        std::sort(r.reachable.begin(), r.reachable.end());
        std::sort(r.unreachable.begin(), r.unreachable.end());
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

// TODO: implement a print mode of all blocks (reachable, unreachable, unclaimed) printed linearly, not grouped under routines
void RoutineMap::dump() const {
    if (empty()) {
        info("--- Empty routine map");
        return;
    }

    vector<Routine> printRoutines = routines;
    // create fake "routines" representing unclaimed blocks for the purpose of printing
    Size unclaimedIdx = 0;
    for (const Block &b : unclaimed) {
        printRoutines.emplace_back(Routine{"unclaimed_"s + to_string(++unclaimedIdx), b});
    }
    std::sort(printRoutines.begin(), printRoutines.end());

    // display routines
    info("--- Routine map containing "s + to_string(routines.size()) + " routines");
    for (const auto &r : printRoutines) {
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
                if (!colideRoutine.isValid() && r.colides(block, false)) 
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

RegisterState::RegisterState(const Address &code, const Address &stack) : RegisterState() {
    setValue(REG_CS, code.segment);
    setValue(REG_IP, code.offset);
    setValue(REG_SS, stack.segment);
    setValue(REG_SP, stack.offset);
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
    entrypoints.push_back(start);
}

string SearchQueue::statusString() const { 
    return "[r"s + to_string(curSearch.routineId) + "/q" + to_string(size()) + "]"; 
} 

int SearchQueue::getRoutineId(const Offset off) const { 
    assert(off < visited.size());
    return visited.at(off); 
}

void SearchQueue::markVisited(const Offset off, const Size length, int id) {
    if (id == NULL_ROUTINE) id = curSearch.routineId;
    fill(visited.begin() + off, visited.begin() + off + length, id);
}

SearchPoint SearchQueue::nextPoint(const bool front) {
    if (!empty()) {
        if (front) {
            curSearch = queue.front();
            queue.pop_front();
        }
        else {
            curSearch = queue.back();
            queue.pop_back();            
        }
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
        debug("Jump destination already visited from id "s + to_string(destId));
    else if (hasPoint(dest, false))
        debug("Queue already contains jump to address "s + dest.toString());
    else { // not claimed by any routine and not yet in queue
        queue.emplace_front(SearchPoint(dest, curSearch.routineId, false, regs));
        debug("Jump destination not yet visited, scheduled visit from id " + to_string(curSearch.routineId) + ", queue size = " + to_string(size()));
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

Executable::Executable(const MzImage &mz, const Address &ep) : 
    codeSize(0), codeData(nullptr), entrypoint(ep.isValid() ? ep : mz.entrypoint()), stack(mz.stackPointer()), reloc(mz.loadSegment())  
{
    const Byte *data = mz.loadModuleData();
    copy(data, data + mz.loadModuleSize(), std::back_inserter(code));
    codeSize = code.size();
    codeData = code.data();
    codeExtents = Block({entrypoint.segment, 0}, Address(SEG_OFFSET(entrypoint.segment) + codeSize));
}

void Executable::searchMessage(const string &msg) const {
    output(csip.toString() + ": " + msg, LOG_ANALYSIS, LOG_DEBUG);
}

// TODO: this should be in the CPU class, RegisterState as injectable drop-in replacement for regular Registers for CPU
Branch Executable::getBranch(const Instruction &i, const RegisterState &regs) const {
    Branch branch;
    branch.isCall = false;
    branch.isUnconditional = false;
    switch (i.iclass) {
    case INS_JMP:
        // conditional jump, put destination into search queue to check out later
        if (opcodeIsConditionalJump(i.opcode)) {
            branch.destination = i.relativeAddress();
            searchMessage("encountered conditional near jump to "s + branch.destination.toString());
        }
        else switch (i.opcode) {
        // unconditional jumps, cannot continue at current location, need to jump now
        case OP_JMP_Jb: 
        case OP_JMP_Jv: 
            branch.destination = i.relativeAddress();
            branch.isUnconditional = true;
            searchMessage("encountered unconditional near jump to "s + branch.destination.toString());
            break;
        case OP_GRP5_Ev:
            searchMessage("unknown near jump target: "s + i.toString());
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
            searchMessage("encountered unconditional far jump to "s + branch.destination.toString());
        }
        else {
            searchMessage("unknown far jump target: "s + i.toString());
            debug(regs.toString());
        }
        break;
    // loops
    case INS_LOOP:
    case INS_LOOPNZ:
    case INS_LOOPZ:
        branch.destination = i.relativeAddress();
        searchMessage("encountered loop to "s + branch.destination.toString());
        break;
    // calls
    case INS_CALL:
        if (i.op1.type == OPR_IMM16) {
            branch.destination = i.relativeAddress();
            branch.isCall = true;
            searchMessage("encountered near call to "s + branch.destination.toString());
        }
        else if (operandIsReg(i.op1.type) && regs.isKnown(i.op1.regId())) {
            branch.destination = {i.addr.segment, regs.getValue(i.op1.regId())};
            branch.isCall = true;
            searchMessage("encountered near call through register to "s + branch.destination.toString());
        }
        else if (operandIsMemImmediate(i.op1.type) && regs.isKnown(REG_DS)) {
            Address memAddr{regs.getValue(REG_DS), i.op1.immval.u16};
            if (codeExtents.contains(memAddr)) {
                Word memWord;
                searchMessage("encountered near call through mem pointer to "s + branch.destination.toString());
                memcpy(&memWord, codeData + memAddr.toLinear(), sizeof(Word));
                branch.destination = Address{i.addr.segment, memWord};
                branch.isCall = true;
            }
            else debug("call destination address outside code extents: " + memAddr.toString());
        }
        else {
            searchMessage("unknown near call target: "s + i.toString());
            debug(regs.toString());
        } 
        break;
    case INS_CALL_FAR:
        if (i.op1.type == OPR_IMM32) {
            branch.destination = Address(i.op1.immval.u32);
            branch.isCall = true;
            searchMessage("encountered far call to "s + branch.destination.toString());
        }
        else {
            searchMessage("unknown far call target: "s + i.toString());
            debug(regs.toString());
        }
        break;
    default:
        throw AnalysisError("Instruction is not a branch: "s + i.toString());
    }
    return branch;
}

// TODO: this should be a member of RegisterState?
void Executable::applyMov(const Instruction &i, RegisterState &regs) const {
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
    else if (operandIsMemImmediate(i.op2.type) && regs.isKnown(prefixRegId(i.prefix))) {
        Address memAddr(regs.getValue(prefixRegId(i.prefix)), i.op2.immval.u16);
        Word memWord;
        Byte memByte;
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
        else searchMessage("mov source address outside code extents: "s + memAddr.toString());
    }
    if (set) {
        searchMessage("encountered move to register: "s + i.toString());
    }
}

void Executable::setEntrypoint(const Address &addr) {
    entrypoint = addr;
}

// TODO: this should be a member of SearchQueue
void Executable::saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents, SearchQueue &sq) const {
    if (!branch.destination.isValid())
        return;

    if (codeExtents.contains(branch.destination)) {
        if (branch.isCall) sq.saveCall(branch.destination, regs);
        else sq.saveJump(branch.destination, regs);
    }
    else {
        searchMessage("branch destination outside code boundaries: "s + branch.destination.toString());
    } 
}

// explore the code without actually executing instructions, discover routine boundaries
// TODO: support multiple code segments
// TODO: identify routines through signatures generated from OMF libraries
// TODO: trace usage of bp register (sub/add) to determine stack frame size of routines
// TODO: store references to potential jump tables (e.g. jmp cs:[bx+0xc08]), if unclaimed after initial search, try treating entries as pointers and run second search before coalescing blocks?
RoutineMap Executable::findRoutines() {
    RegisterState initRegs{entrypoint, stack};
    debug("initial register values:\n"s + initRegs.toString());
    // queue for BFS search
    SearchQueue searchQ(codeSize, SearchPoint(entrypoint, 1, true, initRegs));

    info("Analyzing code in range " + codeExtents);
    // iterate over entries in the search queue
    while (!searchQ.empty()) {
        // get a location from the queue and jump to it
        const SearchPoint search = searchQ.nextPoint();
        csip = search.address;
        const int rid = searchQ.getRoutineId(csip.toLinear());
        searchMessage("starting search at new location, call: "s + to_string(search.isCall) + ", queue: " + searchQ.statusString());
        if (rid != NULL_ROUTINE && !search.isCall) {
            debug("location already claimed by routine "s + to_string(rid) + " and search point did not originate from a call, skipping");
            continue;
        }
        RegisterState regs = search.regs;
        // iterate over instructions at current search location in a linear fashion, until an unconditional jump or return is encountered
        while (true) {
            if (!codeExtents.contains(csip))
                throw AnalysisError("Advanced past loaded code extents: "s + csip.toString());
            const Offset instrOffs = csip.toLinear();
            Instruction i(csip, codeData + instrOffs);
            regs.setValue(REG_CS, csip.segment);
            regs.setValue(REG_IP, csip.offset);
            // mark memory map items corresponding to the current instruction as belonging to the current routine
            searchQ.markVisited(instrOffs, i.length);
            // interpret the instruction
            if (instructionIsBranch(i)) {
                const Branch branch = getBranch(i, regs);
                // if the destination of the branch can be established, place it in the search queue
                saveBranch(branch, regs, codeExtents, searchQ);
                if (branch.isUnconditional) {
                    searchMessage("routine scan interrupted by unconditional branch");
                    break;
                }
            }
            else if (instructionIsReturn(i)) {
                searchMessage("routine scan interrupted by return");
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

    // iterate over discovered memory map and create routine map
    return RoutineMap{searchQ};
}

static string compareStatus(const Instruction &i1, const Instruction &i2, const bool align = true) {
    static const int ALIGN = 50;
    string status = i1.addr.toString() + ": " + i1.toString();
    if (align && status.length() < ALIGN) status.append(ALIGN - status.length(), ' ');
    switch (i1.match(i2)) {
    case INS_MATCH_FULL:     status += " == "; break;
    case INS_MATCH_DIFF:     status += " ~~ "; break;
    case INS_MATCH_DIFFOP1:  status += " ~= "; break;
    case INS_MATCH_DIFFOP2:  status += " =~ "; break;
    case INS_MATCH_MISMATCH: status += " != "; break; 
    }
    status += i2.addr.toString() + ": " + i2.toString();
    return status;
}

bool Executable::compareCode(const RoutineMap &routineMap, const Executable &other) {
    if (routineMap.empty()) {
        throw AnalysisError("Unable to compare executables, routine map is empty");
    }
    verbose("Comparing code between reference (entrypoint "s + entrypoint.toString() + ") and other (entrypoint " + other.entrypoint.toString() + ") executables\n" +
         "Routine map of reference binary has " + to_string(routineMap.size()) + " entries");
    RegisterState initRegs{entrypoint, stack};
    debug("initial register values:\n"s + initRegs.toString());
    // map of equivalent addresses in the compared binaries, seed with the two entrypoints
    std::map<Address, Address> addrMap{{entrypoint, other.entrypoint}};
    // queue of locations (in reference binary) for comparison, likewise seeded with the entrypoint
    SearchQueue compareQ(codeSize, SearchPoint(entrypoint, 1, true, initRegs));
    const int VISITED_ID = 1;
    while (!compareQ.empty()) {
        // get next location for linear scan and comparison of instructions
        const SearchPoint compare = compareQ.nextPoint(false);
        debug("Now at reference location "s + compare.address.toString() + ", queue size = " + to_string(compareQ.size()));
        // get corresponding address in other binary
        csip = compare.address;
        const Routine routine = routineMap.getRoutine(csip);
        // make sure we are inside a reachable block of a know routine from reference binary
        if (!routine.isValid()) {
            debug("Could not find address "s + csip.toString() + " in routine map, skipping location");
            continue;
        }
        Address otherCsip = addrMap[csip];
        if (!otherCsip.isValid()) {
            debug("Could not find equivalent address for "s + csip.toString() + " in address map, skipping location");
            continue;
        }
        const Block compareBlock = routine.blockContaining(compare.address);
        verbose("Comparing reference @ "s + csip.toString() + ", routine " + routine.toString(false) + ", block " + compareBlock.toString(true) +  " with other @ " + otherCsip.toString());
        RegisterState regs = compare.regs;
        // keep comparing subsequent instructions at current location between the reference and other binary
        while (true) {
            if (!contains(csip))
                throw AnalysisError("Advanced past reference code extents: "s + csip.toString());
            if (!other.contains(otherCsip))
                throw AnalysisError("Advanced past other code extents: "s + otherCsip.toString());
            // decode instructions
            Instruction 
                iRef{csip, codeData + csip.toLinear()}, 
                iObj{otherCsip, other.codeData + otherCsip.toLinear()};
            compareQ.markVisited(csip.toLinear(), iRef.length, VISITED_ID);
            verbose(compareStatus(iRef, iObj));
            // compare instructions
            switch (iRef.match(iObj)) {
            case INS_MATCH_FULL:
                break;
            case INS_MATCH_DIFF:
            case INS_MATCH_DIFFOP1:
            case INS_MATCH_DIFFOP2:
            case INS_MATCH_MISMATCH:
                error("Instruction mismatch at "s + compareStatus(iRef, iObj, false));
                return false;
            default:
                throw AnalysisError("Invalid instruction match result");
            }
            // comparison okay, determine where to go next based on instruction contents
            assert(iRef.iclass == iObj.iclass);
            if (instructionIsBranch(iRef)) {
                Branch branch = getBranch(iRef, regs);
                // do not differentiate calls, we don't need the queue to track them since we already have the routine map
                branch.isCall = false; 
                // if the destination of the branch can be established, place it in the queue
                saveBranch(branch, regs, codeExtents, compareQ);
                if (branch.destination.isValid()) {
                    // branch destination in compared binary may be different, so calculate and save in address translation map for future reference
                    // TODO: support saving second set of regs for other
                    Branch objObranch = getBranch(iObj, regs);
                    if (objObranch.destination.isValid()) {
                        // check if mapping not already present in map
                        Address objAddr = addrMap[branch.destination];
                        // not present, add new mapping
                        if (!objAddr.isValid()) { 
                            addrMap[branch.destination] = objObranch.destination;
                            debug("Reference branch destination "s + branch.destination.toString() + " mapped to other " + objObranch.destination.toString());
                        }
                        // already present, make sure it matches the current value
                        else if (objAddr != objObranch.destination) { 
                            error("Reference branch destination "s + branch.destination.toString() + " previously mapped to other " + objAddr.toString() + " now resolves to " + objObranch.destination.toString());
                            return false;
                        }
                    }
                    if (branch.isUnconditional) {
                        searchMessage("routine scan interrupted by unconditional branch");
                        break;
                    }
                }
            }
            // advance to next instruction pair
            csip += iRef.length;
            otherCsip += iObj.length;
            if (csip > compareBlock.end) {
                debug("Reached end of comparison block @ "s + csip.toString());
                break;
            }
        } // iterate over instructions at comparison location
    } // iterate over comparison queue
    verbose("Comparison result positive");
    return true;
}