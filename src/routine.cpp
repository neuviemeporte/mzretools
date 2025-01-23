#include <string>
#include <algorithm>
#include <sstream>
#include <regex>
#include <fstream>
#include <iostream>
#include <queue>

#include "dos/routine.h"
#include "dos/output.h"
#include "dos/analysis.h"
#include "dos/error.h"
#include "dos/util.h"

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

std::string RoutineEntrypoint::toString() const {
    ostringstream str;
    str << "id " << id << ": " << addr.toString();
    if (near) str << " [near]";
    if (!name.empty()) str << " " << name;
    return str.str();
}

bool Routine::isReachable(const Block &b) const {
    return std::find(reachable.begin(), reachable.end(), b) != reachable.end();
}

bool Routine::isUnreachable(const Block &b) const {
    return std::find(unreachable.begin(), unreachable.end(), b) != unreachable.end();
}

Size Routine::reachableSize() const {
    Size ret = 0;
    for (auto &b : reachable) ret += b.size();
    return ret;
}

Size Routine::unreachableSize() const {
    Size ret = 0;
    for (auto &b : unreachable) ret += b.size();
    return ret;
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
    const auto &foundR = std::find_if(reachable.begin(), reachable.end(), [&a](const Block &b){
        return b.contains(a);
    });
    if (foundR != reachable.end()) return *foundR;
    const auto &foundU = std::find_if(unreachable.begin(), unreachable.end(), [&a](const Block &b){
        return b.contains(a);
    });
    if (foundU != unreachable.end()) return *foundU;
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
        debug("Block "s + block.toString() + " colides with extents of routine " + dump(false));
        return true;
    }    
    for (const auto &b : reachable) {
        if (b.intersects(block)) {
            debug("Block "s + block.toString() + " colides with reachable block " + b.toString() + " of routine " + dump(false));
            return true;
        }
    }
    for (const auto &b : unreachable) {
        if (b.intersects(block)) {
            debug("Block "s + block.toString() + " colides with unreachable block " + b.toString() + " of routine " + dump(false));
            return true;
        }
    }    
    return false;
}

string Routine::dump(const bool showChunks) const {
    ostringstream str;
    str << extents.toString(false, true) << ": " << name;
    if (near) str << " [near]";
    else str << " [far]";
    if (ignore) str << " [ignored]";
    if (complete) str << " [complete]";
    if (unclaimed) str << " [unclaimed]";
    if (detached) str << " [detached]";
    if (external) str << " [external]";
    if (assembly) str << " [assembly]";
    if (duplicate) str << " [duplicate]";
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

std::string Routine::toString() const {
    ostringstream str;
    str << name << "/" + entrypoint().toString();
    return str.str();
}

std::vector<Block> Routine::sortedBlocks() const {
    vector<Block> blocks(reachable);
    copy(unreachable.begin(), unreachable.end(), back_inserter(blocks));
    sort(blocks.begin(), blocks.end());
    return blocks;
}

void Routine::recalculateExtents() {
    Address ep = entrypoint();
    assert(ep.isValid());
    // locate main block (the one starting on the entrypoint) and set it as the extents' initial value
    auto mainBlock = std::find_if(reachable.begin(), reachable.end(), [ep](const Block &b){
        return b.begin == ep;
    });
    if (mainBlock == reachable.end()) {
        debug("Unable to find main block for routine "s + dump(false));
        extents.end = extents.begin;
        return;
    }
    extents = *mainBlock;
    // coalesce remaining blocks with the extents if possible
    for (const auto &b : sortedBlocks()) {
        if (b.begin < extents.begin) continue; // ignore blocks that begin before the routine entrypoint
        extents.coalesce(b);
    }
    debug("Calculated routine extents: "s + dump(false));
}

std::smatch Variable::stringMatch(const std::string &str) {
    static const regex VAR_RE{"^([_a-zA-Z0-9]+): ([_a-zA-Z0-9]+) VAR ([0-9a-fA-F]{1,4})"};
    smatch match;
    regex_match(str, match, VAR_RE);
    return match;
}

RoutineMap::RoutineMap(const ScanQueue &sq, const std::vector<Segment> &segs, const Word loadSegment, const Size mapSize) : loadSegment(loadSegment), mapSize(mapSize), ida(false) {
    const Size routineCount = sq.routineCount();
    if (routineCount == 0)
        throw AnalysisError("Attempted to create routine map from search queue with no routines");
    
    setSegments(segs);
    info("Building routine map from search queue contents: "s + to_string(routineCount) + " routines over " + to_string(segments.size()) + " segments");
    routines = sq.getRoutines();

    const Offset startOffset = SEG_TO_OFFSET(loadSegment);
    Offset endOffset = startOffset + mapSize;
    Block b(startOffset);
    prevId = curBlockId = prevBlockId = NULL_ROUTINE;
    Segment curSeg;

    debug("Starting at " + hexVal(startOffset) + ", ending at " + hexVal(endOffset) + ", map size: " + sizeStr(mapSize));
    for (Offset mapOffset = startOffset; mapOffset < endOffset; ++mapOffset) {
        curId = sq.getRoutineId(mapOffset);
        // find segment matching currently processed offset
        Segment newSeg = findSegment(mapOffset);
        if (newSeg.type == Segment::SEG_NONE) {
            error("Unable to find a segment for routine map offset " + hexVal(mapOffset) + ", ignoring remainder");
            endOffset = mapOffset;
            break;
        }
        if (newSeg != curSeg) {
            curSeg = newSeg;
            debug("=== Segment change to " + curSeg.toString());
            // check if segment change made currently open block go out of bounds
            if (!b.begin.inSegment(newSeg.address)) {
                debug("Currently open block " + b.toString() + " does not fit into new segment, forcing close");
                Address closeAddr{mapOffset};
                // force close block before current location
                closeBlock(b, closeAddr, sq);
                // force open a new block at current location if the ID remains the same so that it does not get lost,
                // or erase the block otherwise, so that the rest of this loop opens up a new one instead of closing one that is no longer valid
                if (curId == prevId) {
                    closeAddr.move(curSeg.address);
                    b = Block{closeAddr};
                    debug(closeAddr.toString() + ": forcing opening of new block: " + b.toString() + " for routine_" + to_string(curId));
                }
                else {
                    debug(closeAddr.toString() + ": erasing invalid block: " + b.toString());
                    b = {};
                }
            }
        }
        // convert map offset to segmented address
        Address curAddr{mapOffset};
        curAddr.move(curSeg.address);
        // do nothing as long as the value doesn't change, unless we encounter a routine entrypoint in the middle of a block, in which case we force a block close
        if (curId == prevId && !sq.isEntrypoint(curAddr)) continue;
        // value in map changed (or forced block close because of encounterted entrypoint), new block begins, so close old block and attribute it to a routine if possible
        // the condition prevents attempting to close a (yet non-existent) block at the first byte of the load module
        if (mapOffset != startOffset) closeBlock(b, curAddr, sq); 
        // start new block
        debug(curAddr.toString() + ": starting block for routine_" + to_string(curId));
        b = Block(curAddr);
        curBlockId = curId;
        // last thing to do is memorize current id as previous for discovering when needing to close again
        prevId = curId;
    }
    // close last block finishing on the last byte of the memory map
    debug("Closing final block: " + b.toString() + " at offset " + hexVal(endOffset));
    closeBlock(b, endOffset, sq);
    order();
}


RoutineMap::RoutineMap(const std::string &path, const Word loadSegment) : RoutineMap(loadSegment, 0) {
    static const regex LSTFILE_RE{".*\\.(lst|LST)"};
    const auto fstat = checkFile(path);
    if (!fstat.exists) throw ArgError("File does not exist: "s + path);
    smatch match;
    if (regex_match(path, match, LSTFILE_RE)) loadFromIdaFile(path, loadSegment);
    else loadFromMapFile(path, loadSegment);
    debug("Done, found "s + to_string(routines.size()) + " routines");
    buildUnclaimed();
}

Routine RoutineMap::getRoutine(const Address &addr) const {
    for (const Routine &r : routines) {
        if (r.extents.contains(addr)) return r;
        for (const Block &b : r.reachable) 
            if (b.contains(addr)) return r;
    }
    
    return {};
}

Routine RoutineMap::getRoutine(const std::string &name) const {
    for (const Routine &r : routines)
        if (r.name == name) return r;
    return {};
}

Routine& RoutineMap::getMutableRoutine(const std::string &name) {
    for (Routine &r : routines)
        if (r.name == name) return r;
    return routines.emplace_back(Routine(name, {}));
}

Routine RoutineMap::findByEntrypoint(const Address &ep) const {
    for (const Routine &r : routines)
        if (r.entrypoint() == ep) return r;
    
    return {};
}

// given a block, go over all routines in the map and check if it does not colide (meaning cross over even partially) with any blocks claimed by those routines
Block RoutineMap::findCollision(const Block &b) const {
    for (const Routine &r : routines) {
        for (const Block &rb : r.reachable) if (rb.intersects(b)) return rb;
        for (const Block &ub : r.unreachable) if (ub.intersects(b)) return ub;
    }
    return {};
}

// matches routines by extents only, limited use, mainly unit test for alignment with IDA
Size RoutineMap::match(const RoutineMap &other, const bool onlyEntry) const {
    Size matchCount = 0;
    for (const auto &r : routines) {
        bool routineMatch = false;
        for (const auto &ro : other.routines) {
            if (r.extents == ro.extents || (onlyEntry && r.entrypoint() == ro.entrypoint())) {
                debug("Found routine match for "s + r.dump(false) + " with " + ro.dump(false));
                routineMatch = true;
                matchCount++;
                break;
            }
        }
        if (!routineMatch) {
            debug("Unable to find match for "s + r.dump(false));
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

// utility function used when constructing from an instance of SearchQueue
void RoutineMap::closeBlock(Block &b, const Address &next, const ScanQueue &sq) {
    if (!b.isValid()) return;

    b.end = Address{next.toLinear() - 1};
    b.end.move(b.begin.segment);
    debug(next.toString() + ": closing block " + b.toString() + ", curId = " + to_string(curId) + ", prevId = " + to_string(prevId) 
        + ", curBlockId = " + to_string(curBlockId) + ", prevBlockId = " + to_string(prevBlockId));

    if (!b.isValid())
        throw AnalysisError("Attempted to close invalid block");

    // block contains reachable code
    if (curBlockId > NULL_ROUTINE) { 
        debug("    block is reachable");
        // get handle to matching routine
        assert(curBlockId - 1 < routines.size());
        Routine &r = routines.at(curBlockId - 1);
        assert(r.entrypoint().isValid());
        b.move(r.entrypoint().segment);
        if (sq.isEntrypoint(b.begin)) { // this is the entrypoint block of the routine
            debug("    block starts at routine entrypoint");
        }
        r.reachable.push_back(b);
    }
    // block contains unreachable code or data, attribute to a routine if surrounded by that routine's blocks on both sides
    else if (prevBlockId > NULL_ROUTINE && curId == prevBlockId) { 
        debug("    block is unreachable");
        // get handle to matching routine
        assert(prevBlockId - 1 < routines.size());
        Routine &r = routines.at(prevBlockId - 1);
        assert(r.entrypoint().isValid());
        b.move(r.entrypoint().segment);
        r.unreachable.push_back(b);
    }
    // block is unreachable and unclaimed by any routine
    else {
        debug("    block is unclaimed");
        assert(curBlockId <= NULL_ROUTINE);
        unclaimed.push_back(b);
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
    // sort segments
    std::sort(segments.begin(), segments.end());
}

// this is the string representation written to the mapfile, while Routine::toString() is the stdout representation for info/debugging
std::string RoutineMap::routineString(const Routine &r, const Word reloc) const {
    ostringstream str;
    Block rextent{r.extents};
    if (!rextent.isValid())
        throw AnalysisError("Invalid routine extents for routine " + r.name + ": " + rextent.toString());
    rextent.rebase(reloc);
    if (rextent.begin.segment != rextent.end.segment) 
        throw AnalysisError("Beginning and end of extents of routine " + r.name + " lie in different segments: " + rextent.toString());
    Segment rseg = findSegment(r.extents.begin.segment);
    if (rseg.type == Segment::SEG_NONE) 
        throw AnalysisError("Unable to find segment for routine " + r.name + ", start addr " + r.extents.begin.toString() + ", relocated " + rextent.begin.toString());
    // output routine comments before the actual routine
    for (const string &c : r.comments) str << "# " << c << endl;
    str << r.name << ": " << rseg.name << " " << (r.near ? "NEAR " : "FAR ") << rextent.toHex();
    if (r.unclaimed) {
        str << " U" << rextent.toHex();
    }
    else {
        const auto blocks = r.sortedBlocks();
        for (const auto &b : blocks) {
            Block rblock{b};
            rblock.rebase(reloc);
            if (rblock.begin.segment != rextent.begin.segment)
                throw AnalysisError("Block of routine " + r.name + " lies in different segment than routine extents: " + rblock.toString() + " vs " + rextent.toString());
            if (rblock.begin.segment != rblock.end.segment)
                throw AnalysisError("Beginning and end of block of routine " + r.name + " lie in different segments: " + rblock.toString());
            str << " " << (r.isReachable(b) ? "R" : "U");
            str <<  rblock.toHex();
        }
    }
    if (r.ignore) str << " ignore";
    if (r.complete) str << " complete";
    if (r.external) str << " external";
    if (r.detached) str << " detached";
    if (r.assembly) str << " assembly";
    if (r.duplicate) str << " duplicate";
    return str.str();
}

std::string RoutineMap::varString(const Variable &v, const Word reloc) const {
    ostringstream str;
    if (!v.addr.isValid()) throw ArgError("Invalid variable address for '" + v.name + "' while converting to string");
    const Segment vseg = findSegment(v.addr.segment);
    if (vseg.type == Segment::SEG_NONE) throw AnalysisError("Unable to find segment for variable " + v.name + " / " + v.addr.toString());
    str << v.name << ": " << vseg.name << " VAR " << hexVal(v.addr.offset, false);
    return str.str();
}

void RoutineMap::order() {
    // TODO: coalesce adjacent blocks, see routine_35 of hello.exe: 1415-14f7 R1412-1414 R1415-14f7
    for (auto &r : routines) 
        r.recalculateExtents();
    sort();
}

void RoutineMap::save(const std::string &path, const Word reloc, const bool overwrite) const {
    if (empty()) return;
    if (checkFile(path).exists && !overwrite) throw AnalysisError("Map file already exists: " + path);
    info("Saving routine map (routines = " + to_string(routineCount()) + ") to "s + path + ", reversing relocation by " + hexVal(reloc));
    ofstream file{path};
    if (ida) file << "# ================== !!! WARNING !!! ================== " << endl 
        << "# The content of this mapfile has been deduced from loading an IDA listing, which is not 100% reliable." << endl
        << "# Please verify these values (particularly the load module size and segment addresses), and tweak manually if needed" << endl
        << "# before using this mapfile for further processing by the tooling." << endl;
    file << "#" << endl
         << "# Size of the executable's load module covered by the map" << endl 
         << "#" << endl
         << "Size " << hexVal(mapSize, false) << endl;
    file << "#" << endl
         << "# Discovered segments, one per line, syntax is \"SegmentName Type(CODE/DATA/STACK) Address\"" << endl
         << "#" << endl;
    for (auto s: segments) {
        s.address -= reloc;
        file << s.toString() << endl;
    }
    file << "#" << endl
         << "# Discovered routines, one per line, syntax is \"RoutineName: Segment Type(NEAR/FAR) Extents [R/U]Block1 [R/U]Block2...\"" << endl
         << "# The routine extents is the largest continuous block of instructions attributed to this routine and originating" << endl 
         << "# at the location determined to be the routine's entrypoint." << endl
         << "# Blocks are offset ranges relative to the segment that the routine belongs to, specifying address as belonging to the routine." << endl 
         << "# Blocks starting with R contain code that was determined reachable, U were unreachable but still likely belong to the routine." << endl
         << "# The routine blocks may cover a greater area than the extents if the routine has disconected chunks it jumps into." << endl
         << "#" << endl;
    for (const auto &r : routines) {
        file << routineString(r, reloc) << endl;
    }
    file << "#" << endl
         << "# Discovered variables, one per line, syntax is \"VariableName: Segment VAR OffsetWithinSegment\"" << endl
         << "#" << endl;
    for (const auto &v: vars) {
        file << varString(v, reloc) << endl;
    }
}

// TODO: implement a print mode of all blocks (reachable, unreachable, unclaimed) printed linearly, not grouped under routines
RoutineMap::Summary RoutineMap::getSummary(const bool verbose, const bool brief, const bool format) const {
    ostringstream str;
    Summary sum;
    if (empty()) {
        str << "--- Empty routine map" << endl;
        sum.text = str.str();
        return sum;
    }

    vector<Routine> printRoutines = routines;
    // create fake "routines" representing unclaimed blocks for the purpose of printing
    Size unclaimedIdx = 0;
    for (const Block &b : unclaimed) {
        auto r = Routine{"unclaimed_"s + to_string(++unclaimedIdx), b};
        r.unclaimed = true;
        printRoutines.emplace_back(r);
    }
    std::sort(printRoutines.begin(), printRoutines.end());
    Size mapCount = routineCount();
    str << "--- Routine map containing " << mapCount << " routines" << endl
        << "Size " << sizeStr(mapSize) << endl;
    for (const auto &s : segments) {
        str << s.toString() << endl;
    }

    // display routines, gather statistics
    for (const auto &r : printRoutines) {
        const auto seg = findSegment(r.extents.begin.segment);
        if (seg.type == Segment::SEG_CODE) {
            sum.codeSize += r.size();
            // TODO: f15 does not have meaningful routines in the data segment other than the jump trampolines, 
            // but what about other projects?
            if (r.ignore) { sum.ignoredSize += r.size(); sum.ignoreCount++; }
            if (r.complete) { sum.completedSize += r.size(); sum.completeCount++; }
            if (r.unclaimed) { sum.unclaimedSize += r.size(); sum.unclaimedCount++; }
            if (r.external) { sum.externalSize += r.size(); sum.externalCount++; }
            if (r.detached) { sum.detachedSize += r.size(); sum.detachedCount++; }
            if (r.assembly) { sum.assemblySize += r.size(); sum.assemblyCount++; }
        }
        else if (!r.unclaimed) {
            sum.dataCodeSize += r.size();
            sum.dataCodeCount++;
        }
        // print routine unless hide mode enabled and it's not important - show only uncompleted routines and big enough unclaimed blocks within code segments
        if (!(brief && (r.ignore || r.complete || r.external || r.assembly || r.size() < 3 || seg.type != Segment::SEG_CODE))) {
            if (!format) {
                str << r.dump(verbose);
                if (seg.type == Segment::SEG_DATA) str << " [data]";
                str << endl;
            }
            else {
                str << routineString(r, 0) << endl;
            }
        }
    }
    // consistency check
    if (sum.codeSize > mapSize) throw LogicError("Accumulated code size " + sizeStr(sum.codeSize) + " exceeds total map size of " + sizeStr(mapSize));

    // print statistics
    sum.dataSize = mapSize - sum.codeSize;
    sum.otherSize = sum.ignoredSize - sum.externalSize;
    sum.otherCount = sum.ignoreCount - sum.externalCount;
    sum.ignoredReachableSize = sum.otherSize - sum.detachedSize;
    sum.ignoredReachableCount = sum.otherCount - sum.detachedCount;
    sum.uncompleteSize = sum.codeSize - (sum.completedSize + sum.ignoredSize + sum.assemblySize + sum.unclaimedSize);
    sum.uncompleteCount = mapCount - (sum.completeCount + sum.ignoreCount + sum.dataCodeCount + sum.assemblyCount);
    sum.unaccountedSize = sum.codeSize - (sum.completedSize + sum.uncompleteSize + sum.assemblySize + sum.externalSize 
        + sum.ignoredReachableSize + sum.detachedSize + sum.unclaimedSize);
    sum.unaccountedCount = mapCount - (sum.completeCount + sum.uncompleteCount + sum.assemblyCount + sum.externalCount 
        + sum.ignoredReachableCount + sum.detachedCount + sum.dataCodeCount);
    str << "--- Summary:" << endl
        << "Code size: " << sizeStr(sum.codeSize) << " (" << ratioStr(sum.codeSize, mapSize) << " of load module)" << endl
        << "  Completed: " << sizeStr(sum.completedSize) << " (" << sum.completeCount << " routines, " << ratioStr(sum.completedSize, sum.codeSize) << " of code) - 1:1 rewritten to high level language" << endl
        << "  Uncompleted: " << sizeStr(sum.uncompleteSize) << " (" << sum.uncompleteCount << " routines, " << ratioStr(sum.uncompleteSize, sum.codeSize) << " of code) - routines not yet rewritten which can be" << endl
        << "  Assembly: " << sizeStr(sum.assemblySize) << " (" << sum.assemblyCount << " routines, " << ratioStr(sum.assemblySize, sum.codeSize) << " of code) - impossible to rewrite 1:1" << endl
        << "  Ignored: " << sizeStr(sum.ignoredSize) << " (" << sum.ignoreCount << " routines, " << ratioStr(sum.ignoredSize, sum.codeSize) << " of code) - excluded from comparison" << endl
        << "    External: " << sizeStr(sum.externalSize) << " (" << sum.externalCount << " routines, " << ratioStr(sum.externalSize, sum.ignoredSize) << " of ignored) - e.g. libc library code" << endl
        << "    Other: " << sizeStr(sum.otherSize) << " (" << sum.otherCount << " routines, " << ratioStr(sum.otherSize, sum.ignoredSize) << " of ignored) - code ignored for other reasons" << endl
        << "      Reachable: " << sizeStr(sum.ignoredReachableSize) << " (" << sum.ignoredReachableCount << " routines, " << ratioStr(sum.ignoredReachableSize, sum.otherSize) << " of other) - code which has callers" << endl        
        << "      Unreachable: " << sizeStr(sum.detachedSize) << " (" << sum.detachedCount << " routines, " << ratioStr(sum.detachedSize, sum.otherSize) << " of other) - code which appears unreachable" << endl
        << "  Unclaimed: " << sizeStr(sum.unclaimedSize) << " (" << sum.unclaimedCount << " blocks, " << ratioStr(sum.unclaimedSize, sum.codeSize) << " of code) - holes between routines not covered by map" << endl
        << "  Unaccounted: " << sizeStr(sum.unaccountedSize) << " (" << sum.unaccountedCount << " routines) - consistency check, should be zero" << endl
        << "Data size: " << sizeStr(sum.dataSize) << " (" <<ratioStr(sum.dataSize, mapSize) << " of load module)" << endl
        << "  Routines in data segment: " << sizeStr(sum.dataCodeSize) << ", " << sum.dataCodeCount << " routines" << endl;
    sum.text = str.str();
    return sum;
}

void RoutineMap::setSegments(const std::vector<Segment> &seg) {
    segments = seg;
    std::sort(segments.begin(), segments.end());
}

void RoutineMap::closeUnclaimedBlock(const Block &ub) {
    if (!ub.isValid()) return;
    debug("Closing unclaimed block: " + ub.toString());
    queue<Block> q;
    q.push(ub);
    while (!q.empty()) {
        const Block &curBlock = q.front();
        q.pop();
        // check for collision with a block of a routine, get the block the current block collides with
        const Block collideBlock = findCollision(curBlock);
        if (!collideBlock.isValid()) {
            debug("Non-coliding unclaimed block: " + curBlock.toString() + ", splitting into segments");
            for (const auto &sb : curBlock.splitSegments()) {
                debug("Adding after split: " + sb.toString());
                unclaimed.push_back(sb);
            }
            continue;
        }
        // collsion detected, cut into non-colliding pieces and add to processing queue for reasessment (pieces could still collide with other blocks)
        debug("Detected collision of " + curBlock.toString() + " with " + collideBlock.toString());
        for (const Block &b : curBlock.cut(collideBlock)) {
            debug("Cut down colliding block to " + b.toString());
            q.push(b);
        }
    }
    debug("Finished processing unclaimed block");
}

// recreate list of unclaimed blocks (lost after map is saved to file) from holes in the map coverage after loading the map back from a file
void RoutineMap::buildUnclaimed() {
    if (mapSize == 0) {
        warn("Unknown map size, unable to calculate unclaimed blocks");
        return;
    }
    unclaimed.clear();
    Block b(Address(loadSegment, 0));
    debug("Building unclaimed blocks, mapSize = " + sizeStr(mapSize) + ", load segment: " + hexVal(loadSegment));
    // need routines to be sorted
    sort();
    for (Size ri = 0; ri < routineCount(); ++ri) {
        const Routine &r = getRoutine(ri);
        // in a sorted map, the current routine's beginning is supposed to be at or after the previous one's end
        assert(r.extents.begin >= b.begin);
        debug("Processing routine " + r.name + ": " + r.extents.toString());
        // the current routine starts after the last claimed position
        if (r.extents.begin > b.begin) {
            debug("Gap between the start of this routine and the end of the previous one, potentially unclaimed");
            // check if the potential unclaimed block does not really belong to any routine,
            // it could be a detached chunk outside the main extents
            // caveat: this is just a rough check to see if the beginning of the potential unclaimed block belongs to a routine, need to check the whole block before closing
            const auto chunkRoutine = getRoutine(b.begin);
            if (chunkRoutine.isValid()) {
                const auto chunkBlock = chunkRoutine.blockContaining(b.begin);
                // the found chunk could cover only a part of the potential unclaimed block, so just advance the potential block's beginning past the chunk,
                // mind that this could push the block into the current routine, but that will make the block invalid which we check for before closing
                b.begin = chunkBlock.end + Offset(1);
                debug("Block belongs to chunk " + chunkBlock.toString() + " of routine " + chunkRoutine.name + ", advanced block to " + b.begin.toString());
            }
            // create unclaimed block(s) between the last claimed position and the beginning of this routine
            b.end = r.extents.begin - 1;
            closeUnclaimedBlock(b);
        }
        // start next potential unclaimed block past the end of the current routine
        b = Block(r.extents.end + Offset(1));
        debug("Opening potential unclaimed block past routine end: " + b.toString());
    }
    Address mapEnd{mapSize - 1};
    // check last block, create unclaimed if it does not match the end of the load module
    if (b.begin < mapEnd) {
        b.end = mapEnd;
        b.end.move(b.begin.segment);
        debug("Last block before map end: " + b.toString());
        closeUnclaimedBlock(b);
    }
}

void RoutineMap::storeDataRef(const Address &dr) {
    const Segment ds = findSegment(dr.segment);
    if (ds.type == Segment::SEG_NONE) {
        debug("Unable to save variable at address " + dr.toString() + ", no record of segment at " + hexVal(dr.segment));
        return;
    }
    const size_t idx = vars.size() + 1;
    vars.emplace_back(Variable{"var_" + to_string(idx), dr});
}

Size RoutineMap::segmentCount(const Segment::Type type) const {
    Size ret = 0;
    for (const Segment &s : segments) {
        if (s.type == type) ret++;
    }
    return ret;
}

Segment RoutineMap::findSegment(const Word addr) const {
    for (const auto &s : segments) {
        if (s.address == addr) return s;
    }
    return {};
}

Segment RoutineMap::findSegment(const std::string &name) const {
    for (const auto &s : segments) {
        if (s.name == name) return s;
    }
    return {};
}

Segment RoutineMap::findSegment(const Offset off) const {
    Segment ret;
    // assume sorted segments, find last segment which contains the argument offset
    for (const auto &s : segments) {
        const Offset segOff = SEG_TO_OFFSET(s.address);
        if (segOff <= off && off - segOff <= OFFSET_MAX) ret = s;
    }
    return ret;
}

enum BlockType { BLOCK_NONE, BLOCK_EXTENTS, BLOCK_REACHABLE, BLOCK_UNREACHABLE };

void RoutineMap::loadFromMapFile(const std::string &path, const Word reloc) {
    static const regex 
        RANGE_RE{"([0-9a-fA-F]{1,4})-([0-9a-fA-F]{1,4})"},
        SIZE_RE{"Size\\s+([0-9a-fA-F]+)"};
    debug("Loading routine map from "s + path + ", relocating to " + hexVal(reloc));
    ifstream mapFile{path};
    string line, token;
    Size lineno = 0;
    smatch match;
    while (safeGetline(mapFile, line)) {
        lineno++;
        // ignore comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        // try to interpret as code size
        else if (regex_match(line, match, SIZE_RE)) {
            mapSize = std::stoi(match.str(1), nullptr, 16);
            debug("Parsed map size = " + sizeStr(mapSize));
            continue;
        }
        // try to interpret as a segment
        else if (!(match = Segment::stringMatch(line)).empty()) {
            Segment s(match);
            s.address += reloc;
            debug("Parsed segment: " + s.toString());
            segments.push_back(s);
            continue;
        }
        // try to interpret as a variable
        else if (!(match = Variable::stringMatch(line)).empty()) {
            const string varname = match.str(1), segname = match.str(2), offstr = match.str(3);
            Segment varseg;
            if ((varseg = findSegment(segname)).type == Segment::SEG_NONE) throw ParseError("Line " + to_string(lineno) + ": unknown segment '" + token + "'");
            Address varaddr{varseg.address, static_cast<Word>(stoi(offstr, nullptr, 16))};
            vars.emplace_back(Variable{varname, varaddr});
            continue;
        }
        // otherwise try interpreting as a routine description
        istringstream sstr{line};
        Routine r;
        Segment rseg;
        smatch match;
        int tokenno = 0;
        while (sstr >> token) {
            BlockType bt = BLOCK_NONE;
            tokenno++;
            if (token.empty()) continue;
            switch (tokenno) {
            case 1: // routine name
                if (token.back() != ':') throw ParseError("Line " + to_string(lineno) + ": invalid routine name token syntax '" + token + "'");
                r.name = token.substr(0, token.size() - 1);
                break;
            case 2: // segment name
                if ((rseg = findSegment(token)).type == Segment::SEG_NONE) throw ParseError("Line " + to_string(lineno) + ": unknown segment '" + token + "'");
                break;
            case 3: // near or far
                if (token == "NEAR") r.near = true;
                else if (token == "FAR") r.near = false;
                else throw ParseError("Line " + to_string(lineno) + ": invalid routine type '" + token + "'");
                break;
            case 4: // extents
                bt = BLOCK_EXTENTS;
                break;
            default: // reachable and unreachable blocks follow
                if (token.front() == 'R') bt = BLOCK_REACHABLE;
                else if (token.front() == 'U') bt = BLOCK_UNREACHABLE;
                // TODO: prevent illegal annotation combinations (e.g. external detached) in mapfile                
                else if (token == "ignore") r.ignore = true;
                else if (token == "complete") r.complete = true;
                else if (token == "external") { r.ignore = true; r.external = true; }
                else if (token == "detached") { r.ignore = true; r.detached = true; }
                else if (token == "assembly") { r.assembly = true; }
                else if (token == "duplicate") { r.duplicate = true; }
                else throw ParseError("Line " + to_string(lineno) + ": invalid token: '" + token + "'");
                token = token.substr(1, token.size() - 1);
                break;
            }
            // nothing else to do
            if (bt == BLOCK_NONE) continue; 
            // otherwise process a block
            if (!regex_match(token, match, RANGE_RE)) throw ParseError("Line " + to_string(lineno) + ": invalid routine block '" + token + "'");
            Block block{Address{rseg.address, static_cast<Word>(stoi(match.str(1), nullptr, 16))}, 
                        Address{rseg.address, static_cast<Word>(stoi(match.str(2), nullptr, 16))}};
            // check block for collisions agains rest of routines already in the map as well as the currently built routine
            Routine colideRoutine = colidesBlock(block);
            if (!colideRoutine.isValid() && r.colides(block, false)) 
                colideRoutine = r;
            if (colideRoutine.isValid())
                throw ParseError("Line "s + to_string(lineno) + ": block " + block.toString() + " colides with routine " + colideRoutine.dump(false));
            // add block to routine
            switch(bt) {
            case BLOCK_EXTENTS: 
                r.extents = block; 
                break;
            case BLOCK_REACHABLE: 
                r.reachable.push_back(block); 
                break;
            case BLOCK_UNREACHABLE: 
                r.unreachable.push_back(block);
                break;
            default:
                throw ParseError("Line " + to_string(lineno) + ": unexpected routine block type with '" + token + "'");
            }
        } // iterate over tokens in a routine definition
        if (r.extents.isValid()) {
            debug("routine: "s + r.dump());
            r.id = routines.size() + 1;
            routines.push_back(r);
        }
        else throw ParseError("Line " + to_string(lineno) + ": invalid routine extents " + r.extents.toString());
    } // iterate over mapfile lines

    if (mapSize == 0) throw ParseError("Invalid or undefined map size");
}

// create routine map from IDA listing (.lst) file
// TODO: add collision checks
void RoutineMap::loadFromIdaFile(const std::string &path, const Word reloc) {
    static const string 
        NAME_RE_STR{"[_a-zA-Z0-9]+"},
        OFFSET_RE_STR{"[0-9a-fA-F]{1,4}"},
        ADDR_RE_STR{"(" + NAME_RE_STR + "):(" + OFFSET_RE_STR + ")"},
        LOAD_LEN_STR{"Loaded length: ([0-9a-fA-F]+)h"};
    const regex ADDR_RE{ADDR_RE_STR}, LOAD_LEN_RE{LOAD_LEN_STR};
    debug("Loading IDA routine map from "s + path + ", relocation factor " + hexVal(reloc));
    ida = true;
    ifstream fstr{path};
    string line;
    Size lineno = 0;
    Offset globalPos = 0;
    Word prevOffset = 0;
    Segment curSegment;
    Routine curProc;
    while (safeGetline(fstr, line)) {
        lineno++;
        istringstream sstr{line};
        // first on the line is always the address of the form segName:offset
        string addrStr;
        // ignore empty lines
        if (!(sstr >> addrStr)) continue;
        // next (optionally) is a segment/proc/label/data name
        string nameStr;
        // ignore lines with nothing after the address
        if (!(sstr >> nameStr) || nameStr.empty()) continue;
        // ignore comments except for the special case with the loaded length
        if (nameStr[0] == ';') {
            smatch loadLenMatch;
            if (mapSize == 0 && regex_search(line, loadLenMatch, LOAD_LEN_RE)) {
                mapSize = static_cast<Size>(stoi(loadLenMatch.str(1), nullptr, 16));
                debug("Extracted loaded length: " + hexVal(mapSize) + " from line " + to_string(lineno));
            }
            continue;
        }
        // split the address components
        const auto addrParts = extractRegex(ADDR_RE, addrStr);
        if (addrParts.size() != 2) throw ParseError("Unable to separate address components on line " + to_string(lineno));
        const string segName = addrParts.front(), offsetStr = addrParts.back();
        const Word offsetVal = static_cast<Word>(stoi(offsetStr, nullptr, 16));
        debug("Line " + to_string(lineno) + ": seg=" + segName + ", off=" + hexVal(offsetVal) + ", name='" + nameStr + "', pos=" + hexVal(globalPos));
        // the next token is going to determine the type of the line, e.g. proc/segment/var
        string typeStr;
        // ignore lines with no type discriminator, likely an asm directive or a standalone label
        if (!(sstr >> typeStr) || typeStr.empty()) continue;
        // force lowercase
        std::transform(typeStr.begin(), typeStr.end(), typeStr.begin(), [](unsigned char c){ 
            return std::tolower(c); 
        });
        debug("\ttype: '" + typeStr + "'");
        // segment start
        if (typeStr == "segment") {
            if (curSegment.type != Segment::SEG_NONE) throw ParseError("New segment opening while previous segment " + curSegment.name + " still open on line " + to_string(lineno));
            string alignStr, visStr, clsStr;
            if (!(sstr >> alignStr >> visStr >> clsStr)) throw ParseError("Invalid segment definition on line " + to_string(lineno));
            debug("\tsegment align=" + alignStr + ", vis=" + visStr + ", cls=" + clsStr);
            Segment::Type segType;
            if (clsStr == "'CODE'") segType = Segment::SEG_CODE;
            else if (clsStr == "'DATA'") segType = Segment::SEG_DATA;
            else if (clsStr == "'STACK'") segType = Segment::SEG_STACK;
            else throw ParseError("Unrecognized segment class " + clsStr + " on line " + to_string(lineno));
            // XXX: figuring out the exact position where the new segment starts from the IDA listing alone is hard to impossible - would need to keep a running count of data sizes from db/dup/struc etc. strings, and instruction sizes from asm mnemonics, which are ambiguous due to multiple possible encodings of some instructions. So this is going to be just a rough guess by padding the segment boundary up to paragraph size and the user will probably need to tweak segment addresses manually
            if (globalPos != 0) globalPos += PARAGRAPH_SIZE - (globalPos % PARAGRAPH_SIZE);
            Address segAddr{globalPos};
            segAddr.normalize();
            segAddr.segment += reloc;
            curSegment = Segment{nameStr, segType, segAddr.segment};
            debug("\tinitialized new segment at address " + hexVal(curSegment.address) + ", globalPos=" + hexVal(globalPos));
            prevOffset = 0;
        }
        // segment end
        else if (typeStr == "ends") {
            debug("\tclosing segment " + curSegment.name);
            segments.push_back(curSegment);
            curSegment = {};
        }
        // routine start
        else if (typeStr == "proc") {
            string procType;
            sstr >> procType;
            if (curProc.isValid()) throw ParseError("Opening new proc '" + nameStr + "' while previous '" + curProc.name + "' still open on line " + to_string(lineno));
            curProc = Routine{nameStr, Block{Address{curSegment.address, offsetVal}}};
            if (procType == "far") curProc.near = false;
            debug("Opened proc: " + curProc.toString());
        }
        // routine end
        else if (typeStr == "endp") {
            if (!curProc.isValid()) throw ParseError("Closing proc '" + nameStr + "' without prior open on line " + to_string(lineno));
            if (curProc.name != nameStr) throw ParseError("Closing proc '" + nameStr + "' while '" + curProc.name + "' open on line " + to_string(lineno));
            // XXX: likewise, this will be off due to IDA placing the endp on the same offset as the last instruction of the proc, whose length we do not know
            curProc.extents.end = Address{curSegment.address, offsetVal};
            routines.push_back(curProc);
            curProc = {};
        }
        // simple data
        // TODO: support structs
        else if (typeStr == "db" || typeStr == "dw" || typeStr == "dd") {
            vars.emplace_back(Variable{nameStr, Address{curSegment.address, offsetVal}});
        }

        if (offsetVal < prevOffset) throw ParseError("Offsets going backwards (" + hexVal(prevOffset) + "->" + hexVal(offsetVal) + ") on line " + to_string(lineno));
        globalPos += offsetVal - prevOffset;
        prevOffset = offsetVal;
    } // iterate over listing lines
}
