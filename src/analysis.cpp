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

static void warn(const string &msg) {
    output("WARNING: "s + msg, LOG_ANALYSIS, LOG_WARN);
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
    str << extents.toString(false, true) << ": " << name;
    if (!showChunks || isUnchunked()) return str.str();

    auto blocks = sortedBlocks();
    for (const auto &b : blocks) {
        str << endl << "\t" << b.toString(false, true) << ": ";
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

RoutineMap::RoutineMap(const ScanQueue &sq, const Word loadSegment, const Size codeSize) : reloc(loadSegment), codeSize(codeSize) {
    // XXX: debugging purpose only, remove later
    sq.dumpVisited("search_queue.dump", SEG_TO_OFFSET(loadSegment), codeSize);
    const Size routineCount = sq.routineCount();
    if (routineCount == 0)
        throw AnalysisError("Attempted to create routine map from search queue with no routines");
    info("Building routine map from search queue contents ("s + to_string(routineCount) + " routines)");
    routines = sq.getRoutines();

    const Offset startOffset = SEG_TO_OFFSET(loadSegment);
    const Offset endOffset = startOffset + codeSize;
    Block b(startOffset);
    prevId = curBlockId = prevBlockId = NULL_ROUTINE;

    for (Offset mapOffset = startOffset; mapOffset < endOffset; ++mapOffset) {
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
    closeBlock(b, endOffset, sq);

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

// TODO: instead of requiring the relocation factor as an argment, perhaps save it to the mapfile?
RoutineMap::RoutineMap(const std::string &path, const Word reloc) : reloc(reloc) {
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
void RoutineMap::closeBlock(Block &b, const Offset off, const ScanQueue &sq) {
    if (!b.isValid() || off == 0) 
        return;

    b.end = Address{off - 1};
    debug(hexVal(off) + ": closing block starting at " + b.begin.toString() + ", curId = " + to_string(curId) + ", prevId = " + to_string(prevId) 
        + ", curBlockId = " + to_string(curBlockId) + ", prevBlockId = " + to_string(prevBlockId));
    if (!b.isValid())
        throw AnalysisError("Attempted to close invalid block");

    // block contains reachable code
    if (curBlockId != NULL_ROUTINE) { 
        // get handle to matching routine
        assert(curBlockId - 1 < routines.size());
        Routine &r = routines.at(curBlockId - 1);
        const Word epSeg = r.entrypoint().segment;
        if (sq.isEntrypoint(b.begin)) { // this is the entrypoint block of the routine
            debug("block starts at routine entrypoint");
        }
        // add reachable block
        // addresses created from map offsets lose the original segment information, 
        // so move the block into the segment of the routine's entrypoint, if possible
        assert(r.entrypoint().isValid());
        r.reachable.push_back(moveBlock(b, r.entrypoint().segment));
    }
    // block contains unreachable code or data, attribute to a routine if surrounded by that routine's blocks on both sides
    else if (prevBlockId != NULL_ROUTINE && curId == prevBlockId) { 
        // get handle to matching routine
        assert(prevBlockId - 1 < routines.size());
        Routine &r = routines.at(prevBlockId - 1);
        assert(r.entrypoint().isValid());
        r.unreachable.push_back(moveBlock(b, r.entrypoint().segment));
    }
    // block is unreachable and unclaimed by any routine
    else {
        assert(curBlockId == NULL_ROUTINE);
        debug("closed unclaimed block " + b.toString());
        // try to move the unclaimed block into the executable's original load segment
        unclaimed.push_back(moveBlock(b, reloc));
    }

    // remember the id of the block we just closed
    prevBlockId = curBlockId;
}

Block RoutineMap::moveBlock(const Block &b, const Word segment) const {
    Block block(b);
    if (block.inSegment(segment)) {
        block.move(segment);
    }
    else {
        warn("Unable to move block "s + block.toString() + " to segment " + hexVal(segment));
    }
    return block;
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

void RoutineMap::save(const std::string &path, const bool overwrite) const {
    if (empty()) return;
    if (checkFile(path).exists && !overwrite) throw AnalysisError("Map file already exists: " + path);
    info("Saving routine map (size = " + to_string(size()) + ") to "s + path);
    ofstream file{path};
    for (auto s: segments) {
        s.value -= reloc;
        file << s.toString() << endl;
    }
    for (const auto &r : routines) {
        Block rextent{r.extents};
        rextent.rebase(reloc);
        file << r.name << ": e" << rextent.toString(true, false);
        const auto blocks = r.sortedBlocks();
        for (const auto &b : blocks) {
            Block rblock{b};
            rblock.rebase(reloc);
            if (r.isReachable(b))
                file << " r" << rblock.toString(true, false);
            else
                file << " u" << rblock.toString(true, false);
        }
        file << endl;
    }
}

// TODO: implement a print mode of all blocks (reachable, unreachable, unclaimed) printed linearly, not grouped under routines
string RoutineMap::dump() const {
    ostringstream str;
    if (empty()) {
        str << "--- Empty routine map";
        return str.str();;
    }

    vector<Routine> printRoutines = routines;
    // create fake "routines" representing unclaimed blocks for the purpose of printing
    Size unclaimedIdx = 0;
    for (const Block &b : unclaimed) {
        printRoutines.emplace_back(Routine{"unclaimed_"s + to_string(++unclaimedIdx), b});
    }
    std::sort(printRoutines.begin(), printRoutines.end());

    str << "--- Routine map containing " << routines.size() << " routines" << endl;

    for (const auto &s : segments) {
        str << s.toString() << endl;
    }

    // display routines
    for (const auto &r : printRoutines) {
        str << r.toString(true) << endl;
    }
    return str.str();
}

void RoutineMap::setSegments(const std::vector<Segment> &seg) {
    segments = seg;
}

Size RoutineMap::segmentCount(const Segment::Type type) const {
    Size ret = 0;
    for (const Segment &s : segments) {
        if (s.type == type) ret++;
    }
    return ret;
}

void RoutineMap::loadFromMapFile(const std::string &path) {
    debug("Loading routine map from "s + path);
    ifstream mapFile{path};
    string line, token;
    Size lineno = 0;
    bool colideOk = true;
    while (safeGetline(mapFile, line) && colideOk) {
        lineno++;
        try {
            Segment s(line);
            s.value += reloc;
            segments.push_back(s);
            continue;
        }
        catch (ArgError &e) {}
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
                Block block{token.substr(1, token.size() - 1)};
                block.relocate(reloc);
                block.move(reloc);
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
    debug("Loading IDA routine map from "s + path + ", relocation factor " + hexVal(reloc));
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
        // TODO: get rid of harcoded segment name
        if (addrStr.find("seg000") != string::npos) {
            addrStr = addrStr.substr(2,9);
            addrStr[0] = '0';
            curAddr = Address{addrStr};
            curAddr.relocate(reloc);
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

bool OffsetMap::codeMatch(const Address from, const Address to) {
    if (!(from.isValid() && to.isValid()))
        return false;

    // mapping already exists
    if (codeMap.count(from) > 0) {
        if (codeMap[from] == to) {
            debug("Existing code address mapping " + from.toString() + " -> " + to.toString() + " matches");
            return true;
        }
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

Segment::Segment(const std::string &str) : type(SEG_NONE) {
    static const regex 
        CODE_RE{"^CODE/0x([0-9a-fA-F]{1,4})"}, 
        DATA_RE{"^DATA/0x([0-9a-fA-F]{1,4})"},
        STACK_RE{"^STACK/0x([0-9a-fA-F]{1,4})"};
    
    smatch match;
    if (regex_match(str, match, CODE_RE)) type = SEG_CODE;
    else if (regex_match(str, match, DATA_RE)) type = SEG_DATA;
    else if (regex_match(str, match, STACK_RE)) type = SEG_STACK;

    const string valstr = match.str(1);
    if (type != SEG_NONE) value = stoi(valstr, nullptr, 16);
    else throw ArgError("Invalid segment string: " + str);
}

std::string Segment::toString() const {
    ostringstream str;
    switch (type) {
    case SEG_CODE:  str << "CODE/"; break;
    case SEG_DATA:  str << "DATA/"; break;
    case SEG_STACK: str << "STACK/"; break;
    default:        str << "???""/"; break; 
    }
    str << hexVal(value);
    return str.str();
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
    entrypoints.emplace_back(RoutineEntrypoint(start, seed.routineId));
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

Destination ScanQueue::nextPoint(const bool front) {
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
        // assign automatic names to routines
        if (ep.addr == startAddress()) r.name = "start";
        else r.name = "routine_"s + to_string(ep.id);
    }   
    return routines;
}

// function call, create new routine at destination if either not visited, 
// or visited but destination was not yet discovered as a routine entrypoint and this call now takes precedence and will replace it
bool ScanQueue::saveCall(const Address &dest, const RegisterState &regs) {
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
        queue.emplace_front(Destination(dest, newRoutineId, true, regs));
        entrypoints.emplace_back(RoutineEntrypoint(dest, newRoutineId));
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
        queue.emplace_front(Destination(dest, curSearch.routineId, false, regs));
        debug("Jump destination not yet visited, scheduled visit from routine " + to_string(curSearch.routineId) + ", queue size = " + to_string(size()));
        return true;
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

VariantMap::VariantMap() {
}

VariantMap::VariantMap(const std::string &path) {
    auto stat = checkFile(path);
    if (!stat.exists) throw ArgError("Variant map file does not exist: " + path);
    ifstream file{path};
    string bucketLine;
    Size maxDepth = 0;
    // iterate over lines, each is a bucket of equivalent instruction variants
    while (safeGetline(file, bucketLine)) {
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

static vector<string> stringify(const InstructionSequence &iseq, const int count = -1) {
    vector<string> ret;
    int idx = 0;
    for (const auto &i : iseq) { 
        if (count > 0 && (idx++ > count)) break;
        ret.push_back(i.toString()); 
    }
    return ret;
}

MatchDepth VariantMap::checkMatch(const InstructionSequence &left, const InstructionSequence &right) {
    Variant leftVariant = stringify(left), rightVariant = stringify(right);
    return checkMatch(leftVariant, rightVariant);
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

Executable::Executable(const MzImage &mz) : 
    code(mz.loadSegment(), mz.loadModuleData(), mz.loadModuleSize()),
    loadSegment(mz.loadSegment()),
    codeSize(mz.loadModuleSize()),
    stack(mz.stackPointer()), 
    codeExtents({loadSegment, Word(0)}, Address(SEG_TO_OFFSET(loadSegment) + codeSize))
{
    // relocate entrypoint and stack
    setEntrypoint(mz.entrypoint());
    stack.relocate(loadSegment);
    debug("Loaded executable data into memory, code at "s + codeExtents.toString() + ", relocated entrypoint " + entrypoint().toString() + ", stack " + stack.toString());
}

void Executable::setEntrypoint(const Address &addr) {
    ep = addr;
    ep.relocate(loadSegment);
}

Executable::Context::Context(const Executable &other, const AnalysisOptions &opt, const Size maxData) 
        : other(other), options(opt), offMap(maxData)
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
    const Address &addr = i.addr;
    switch (i.iclass) {
    case INS_JMP:
        // conditional jump, put destination into search queue to check out later
        if (opcodeIsConditionalJump(i.opcode)) {
            branch.destination = i.relativeAddress();
            searchMessage(addr, "encountered conditional near jump to "s + branch.destination.toString());
        }
        else switch (i.opcode) {
        // unconditional jumps, cannot continue at current location, need to jump now
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
            searchMessage(addr, "encountered far call to "s + branch.destination.toString());
        }
        else {
            searchMessage(addr, "unknown far call target: "s + i.toString());
            debug(regs.toString());
        }
        break;
    default:
        throw AnalysisError("Instruction is not a branch: "s + i.toString());
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
        if (i.op1.type == OPR_REG_DS) storeSegment({Segment::SEG_DATA, regs.getValue(REG_DS)});
        else if (i.op1.type == OPR_REG_SS) storeSegment({Segment::SEG_STACK, regs.getValue(REG_SS)});
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
    if (align && status.length() < ALIGN) status.append(ALIGN - status.length(), ' ');
    if (i1.isValid() && i2.isValid()) {
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
    if (i2.isValid()) {
        status += i2.addr.toString() + ": " + i2.toString(align);
    }
    return status;
}

// dictionary of equivalent instruction sequences for variant-enabled comparison
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

Executable::ComparisonResult Executable::instructionsMatch(Context &ctx, Instruction ref, Instruction obj) {
    if (ctx.options.ignoreDiff) return CMP_MATCH;

    auto insResult = ref.match(obj);
    if (insResult == INS_MATCH_FULL) return CMP_MATCH;

    bool match = false;
    // instructions differ in value of immediate or memory offset
    if (insResult == INS_MATCH_DIFFOP1 || insResult == INS_MATCH_DIFFOP2) {
        const auto &refop = (insResult == INS_MATCH_DIFFOP1 ? ref.op1 : ref.op2);
        const auto &objop = (insResult == INS_MATCH_DIFFOP1 ? obj.op1 : obj.op2);
        if (ref.isBranch()) {
            const Branch 
                refBranch = getBranch(ref), 
                objObranch = getBranch(obj);
            if (refBranch.destination.isValid() && objObranch.destination.isValid()) {
                match = ctx.offMap.codeMatch(refBranch.destination, objObranch.destination);
            }
            // special case of jmp vs jmp short - allow only if variants enabled
            // TODO: this will also mismatch on jump GRP encoding
            if (match && ref.isUnconditionalJump() && ref.opcode != obj.opcode) {
                if (ctx.options.variant) {
                    verbose(output_color(OUT_YELLOW) + compareStatus(ref, obj, true, INS_MATCH_DIFF) + output_color(OUT_DEFAULT));
                    ctx.otherCsip += obj.length;
                    return CMP_VARIANT;
                }
                else return CMP_MISMATCH;
            }
        }
        else if (operandIsMemWithOffset(refop.type)) {
            SOffset refOfs = ref.memOffset(), objOfs = obj.memOffset();
            switch (ref.memSegmentId()) {
            case REG_DS:
                match = ctx.offMap.dataMatch(refOfs, objOfs);
                break;
            case REG_SS:
                // in strict mode, the stack is expected to have exactly matching layout, with no translation
                if (!ctx.options.strict) match = ctx.offMap.stackMatch(refOfs, objOfs);
                break;
            }
        }
        else if (operandIsImmediate(refop.type)) {
            if (!ctx.options.strict) {
                debug("Ignoring immediate value difference in loose mode");
                return CMP_DIFFVAL;
            } 
        }
    }
    
    if (match) return CMP_MATCH;

    // advance an instruction and push the result into a vector if still within code extents
    auto pushNext = [](Instruction &i, const Block &extents, const Memory &code, InstructionSequence &dest){
        if (!i.isValid()) return;
        i.addr += i.length;
        if (extents.contains(i.addr)) {
            i = Instruction(i.addr, code.pointer(i.addr));
            dest.push_back(i);
        }
        else i = Instruction();
    };

    // check for a variant match if allowed by options
    if (ctx.options.variant) {
        const Size mapDepth = variants.maxDepth();
        InstructionSequence refSeq{ref}, objSeq{obj};
        // gather as many instructions from both executables as it takes to compare to a longest instruction sequence in the variant map
        for (Size i = 0; i < mapDepth; ++i) {
            pushNext(ref, codeExtents, code, refSeq);
            pushNext(obj, ctx.other.codeExtents, ctx.other.code, objSeq);
        }
        // check if the sequences match by the variant lookup map
        const VariantMap::MatchDepth md = variants.checkMatch(refSeq, objSeq);
        if (!md.isMatch()) return CMP_MISMATCH;
        // compose status string for verbose mode
        debug("Found variant match for " + joinString(stringify(refSeq, md.left), ';') + " with " + joinString(stringify(objSeq, md.right), ';');
        ostringstream statusStr;
        for (int i = 0; i < std::max(md.left, md.right); ++i) {
            if (i) statusStr << endl;
            Instruction r = i < refSeq.size() ? refSeq[i] : {};
            Instruction o = i < objSeq.size() ? objSeq[i] : {};
            statusStr << compareStatus(r, o, true)
        }
        verbose(output_color(OUT_YELLOW) + statusStr.str() + output_color(OUT_DEFAULT));
        return CMP_VARIANT;
    }
    // all matching possibilites exhausted    
    return CMP_MISMATCH;
}

void Executable::storeSegment(const Segment &seg) {
    // ignore segments which are already known
    auto found = std::find(segments.begin(), segments.end(), seg);
    if (found != segments.end()) return;
    // check if an existing segment of a different type has the same address
    found = std::find_if(segments.begin(), segments.end(), [&](const Segment &s){ 
        return s.value == seg.value; 
    });
    if (found != segments.end()) {
        const Segment &existing = *found;
        // existing code segments cannot share an address with another segment
        if (existing.type == Segment::SEG_CODE) return;
        // a new data segment trumps an existing stack segment
        else if (existing.type == Segment::SEG_STACK && seg.type == Segment::SEG_DATA) {
            segments.erase(found);
        }
        else return;
    }
    debug("Found new segment: " + seg.toString());
    segments.push_back(seg);
}

void Executable::diffContext(Address a1, const Memory &code2, Address a2) const {
    static const int CONTEXT_COUNT = 10; // show 10 subsequent instructions after a mismatch
    verbose("--- Context information for " + to_string(CONTEXT_COUNT) + " additional instructions after mismatch location:");
    Instruction i1, i2;
    for (int i = 0; i <= CONTEXT_COUNT; ++i) {
        i1 = Instruction{a1, code.pointer(a1)},
        i2 = Instruction{a2, code2.pointer(a2)};
        if (i != 0) verbose(compareStatus(i1, i2, true));
        a1 += i1.length;
        a2 += i2.length;
    }
}

// TODO: this should be a member of SearchQueue
bool Executable::saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents, ScanQueue &sq) const {
    if (!branch.destination.isValid())
        return false;

    if (codeExtents.contains(branch.destination)) {
        return branch.isCall ? sq.saveCall(branch.destination, regs) : sq.saveJump(branch.destination, regs);
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
    storeSegment({Segment::SEG_STACK, stack.segment});
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
        storeSegment({Segment::SEG_CODE, csip.segment});
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
    auto ret = RoutineMap{searchQ, loadSegment, codeSize};
    ret.setSegments(segments);
    return ret;
}

bool Executable::compareCode(const RoutineMap &routineMap, const Executable &other, const AnalysisOptions &options) {
    verbose("Comparing code between reference (entrypoint "s + entrypoint().toString() + ") and other (entrypoint " + other.entrypoint().toString() + ") executables");
    debug("Routine map of reference binary has " + to_string(routineMap.size()) + " entries");
    Context ctx{other, options, routineMap.segmentCount(Segment::SEG_DATA)};
    // map of equivalent addresses in the compared binaries, seed with the two entrypoints
    ctx.offMap.setCode(entrypoint(), other.entrypoint());
    const RoutineId VISITED_ID = 1;
    // queue of locations (in reference binary) for comparison, likewise seeded with the entrypoint
    // TODO: use routine map to fill compareQ in advance
    // TODO: implement register value tracing
    ScanQueue compareQ(Destination(entrypoint(), VISITED_ID, true, {}));
    while (!compareQ.empty()) {
        // get next location for linear scan and comparison of instructions
        const Destination compare = compareQ.nextPoint(false);
        debug("Now at reference location "s + compare.address.toString() + ", queue size = " + to_string(compareQ.size()));
        // when entering a routine, forget all the current stack offset mappings
        if (compare.isCall) {
            ctx.offMap.resetStack();
        }
        ctx.csip = compare.address;
        if (options.stopAddr.isValid() && ctx.csip >= options.stopAddr) {
            verbose("Reached stop address: " + ctx.csip.toString());
            goto success;
        }        
        if (compareQ.getRoutineId(ctx.csip.toLinear()) != NULL_ROUTINE) {
            debug("Location already compared, skipping");
            continue;
        }
        // get corresponding address in other binary
        ctx.otherCsip = ctx.offMap.getCode(ctx.csip);
        if (!ctx.otherCsip.isValid()) {
            debug("Could not find equivalent address for "s + ctx.csip.toString() + " in address map, skipping location");
            continue;
        }
        Routine routine{"unknown", {}};
        Block compareBlock;
        if (!routineMap.empty()) { // comparing with a map
            routine = routineMap.getRoutine(ctx.csip);
            // make sure we are inside a reachable block of a know routine from reference binary
            if (!routine.isValid()) {
                debug("Could not find address "s + ctx.csip.toString() + " in routine map, skipping location");
                continue;
            }
            compareBlock = routine.blockContaining(compare.address);
            verbose("--- Now @"s + ctx.csip.toString() + ", routine " + routine.toString(false) + ", block " + compareBlock.toString(true) +  ", compared @" + ctx.otherCsip.toString());
        }
        else { // comparing without a map
            verbose("--- Now @"s + ctx.csip.toString() + ", compared @" + ctx.otherCsip.toString());
        }
        // keep comparing subsequent instructions at current location between the reference and other binary
        Size skipCount = 0;
        while (true) {
            if (!contains(ctx.csip))
                throw AnalysisError("Advanced past reference code extents: "s + ctx.csip.toString());
            if (!other.contains(ctx.otherCsip))
                throw AnalysisError("Advanced past other code extents: "s + ctx.otherCsip.toString());
            // decode instructions
            Instruction 
                iRef{ctx.csip, code.pointer(ctx.csip)}, 
                iObj{ctx.otherCsip, other.code.pointer(ctx.otherCsip)};
            // mark this instruction as visited, unlike the routine finding algorithm, we do not differentiate between routine IDs
            compareQ.setRoutineId(ctx.csip.toLinear(), iRef.length, VISITED_ID);
            // compare instructions
            const auto matchType = instructionsMatch(ctx, iRef, iObj);
            switch (matchType) {
            case CMP_MATCH:
                // an instruction match resets the allowed skip counter
                verbose(compareStatus(iRef, iObj, true));
                skipCount = 0;
                break;
            case CMP_MISMATCH:
                // attempt to skip the difference, if permitted by the options
                skipCount++;
                if (skipCount > options.skipDiff) {
                    verbose(output_color(OUT_RED) + compareStatus(iRef, iObj, true) + output_color(OUT_DEFAULT));
                    error("Instruction mismatch in routine " + routine.name + " at " + compareStatus(iRef, iObj, false));
                    diffContext(ctx.csip, other.code, ctx.otherCsip);
                    return false;
                }
                else {
                    verbose(output_color(OUT_YELLOW) + compareStatus(iRef, iObj, true) + output_color(OUT_DEFAULT));
                }
                break;
            case CMP_DIFFVAL:
                verbose(output_color(OUT_YELLOW) + compareStatus(iRef, iObj, true) + output_color(OUT_DEFAULT));
                break;
            }

            // comparison result okay (instructions match or skip permitted), interpret the instructions
            if (!options.noCall && iRef.isCall() && iObj.isCall()) {
                const Branch 
                    refBranch = getBranch(iRef, {}),
                    objBranch = getBranch(iObj, {});
                // if the destination of the branch can be established, place it in the compare queue
                if (saveBranch(refBranch, {}, codeExtents, compareQ)) {
                    // if the branch destination was accepted, save the address mapping of the branch destination between the reference and object
                    ctx.offMap.codeMatch(refBranch.destination, objBranch.destination);
                }
            }
            else if (iRef.isReturn()) {
                searchMessage(ctx.csip, "linear compare scan interrupted by return");
                break;
            }
            if (compareBlock.isValid() && ctx.csip > compareBlock.end) {
                debug("Reached end of comparison block @ "s + ctx.csip.toString());
                break;
            }
            // advance to next instruction in the reference executable
            ctx.csip += iRef.length;
            if (options.stopAddr.isValid() && ctx.csip >= options.stopAddr) {
                verbose("Reached stop address: " + ctx.csip.toString());
                goto success;
            }
            // advance to next instruction in the compared executable
            switch (matchType) {
            case CMP_MISMATCH:
                // if the instructions did not match and we still got here, that means we are in difference skipping mode, 
                // so don't advance to the next instruction in the compared (other) binary, maybe the next one from the reference will match
                // TODO: support skipping of object as well?
                debug("Skipping over instruction mismatch, allowed " + to_string(skipCount) + " out of " + to_string(options.skipDiff));
                break;
            case CMP_VARIANT:
                // in case of a variant match, the instruction pointer in the compared binary will have already been advanced by instructionsMatch()
                debug("Variant match detected, comparison will continue at " + ctx.otherCsip.toString());
                break;
            default:
                ctx.otherCsip += iObj.length;
                break;
            }
        } // iterate over instructions at comparison location
    } // iterate over comparison location queue

success:
    verbose(output_color(OUT_GREEN) + "Comparison result positive" + output_color(OUT_DEFAULT));
    return true;
}