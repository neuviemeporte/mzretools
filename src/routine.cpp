#include <string>
#include <algorithm>
#include <sstream>
#include <regex>
#include <fstream>

#include "dos/routine.h"
#include "dos/output.h"
#include "dos/analysis.h"
#include "dos/error.h"
#include "dos/util.h"

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

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

Block Routine::nextReachable(const Address &from) const {
    const auto &found = std::find_if(reachable.begin(), reachable.end(), [&from](const Block &b){
        return b.begin >= from;
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
