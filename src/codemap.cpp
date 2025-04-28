#include "dos/codemap.h"
#include "dos/error.h"
#include "dos/util.h"
#include "dos/output.h"
#include "dos/scanq.h"

#include <fstream>

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

#ifdef DEBUG
#define PARSE_DEBUG(msg) debug(msg)
#else
#define PARSE_DEBUG(msg)
#endif

std::string Variable::toString(const bool brief) const { 
    ostringstream oss;
    oss << name << "/" << addr.toString();
    if (!brief && external) oss << " external";
    if (!brief && bss) oss << " bss";
    return oss.str();
}

void CodeMap::blocksFromQueue(const ScanQueue &sq, const bool unclaimedOnly) {
    const Offset startOffset = SEG_TO_OFFSET(loadSegment);
    Offset endOffset = startOffset + mapSize;
    Block b(startOffset);
    prevId = curBlockId = prevBlockId = NULL_ROUTINE;
    Segment curSeg;

    debug("Starting at " + hexVal(startOffset) + ", ending at " + hexVal(endOffset) + ", map size: " + sizeStr(mapSize));
    for (Offset mapOffset = startOffset; mapOffset < endOffset; ++mapOffset) {
        curId = sq.getRoutineIdx(mapOffset);
        // find segment matching currently processed offset
        Segment newSeg = findSegment(mapOffset);
        if (newSeg.type == Segment::SEG_NONE) {
            warn("Unable to find segment for offset " + hexVal(mapOffset) + " while generating code map");
            // attempt to find any segment past the offset, ignore the area in between
            newSeg = findSegment(mapOffset, true);
            if (newSeg.type == Segment::SEG_NONE) {
                error("No more segments, ignoring remainder of address space");
                endOffset = mapOffset;
                break;
            }
            debug("Skipping to next segment: " + newSeg.toString() + ", offset " + hexVal(mapOffset) + ", forcing close of block " + b.toString());
            closeBlock(b, Address{mapOffset}, sq, unclaimedOnly);
            b = Block{};
            mapOffset = SEG_TO_OFFSET(newSeg.address);
        }
        if (newSeg != curSeg) {
            curSeg = newSeg;
            debug("=== Segment change to " + curSeg.toString());
            // check if segment change made currently open block go out of bounds
            if (!b.begin.inSegment(newSeg.address)) {
                debug("Currently open block " + b.toString() + " does not fit into new segment, forcing close");
                Address closeAddr{mapOffset};
                // force close block before current location
                closeBlock(b, closeAddr, sq, unclaimedOnly);
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
        if (mapOffset != startOffset) closeBlock(b, curAddr, sq, unclaimedOnly); 
        // start new block
        debug(curAddr.toString() + ": starting block for routine_" + to_string(curId));
        b = Block(curAddr);
        curBlockId = curId;
        // last thing to do is memorize current id as previous for discovering when needing to close again
        prevId = curId;
    }
    // close last block finishing on the last byte of the memory map
    debug("Closing final block: " + b.toString() + " at offset " + hexVal(endOffset));
    closeBlock(b, endOffset, sq, unclaimedOnly);
}

CodeMap::CodeMap(const ScanQueue &sq, const std::vector<Segment> &segs, const std::set<Variable> &vars, const Word loadSegment, const Size mapSize) : loadSegment(loadSegment), mapSize(mapSize), ida(false) {
    const Size routineCount = sq.routineCount();
    if (routineCount == 0)
        throw AnalysisError("Attempted to create code map from search queue with no routines");
    
    setSegments(segs);
    info("Building code map from search queue contents: "s + to_string(routineCount) + " routines over " + to_string(segments.size()) + " segments");
    routines = sq.getRoutines();
    blocksFromQueue(sq, false);
    for (const auto &v : vars) storeVariable(v);
    order();
}

CodeMap::CodeMap(const std::string &path, const Word loadSegment, const Type type) : CodeMap(loadSegment, 0) {
    const auto fstat = checkFile(path);
    if (!fstat.exists) throw ArgError("File does not exist: "s + path);
    switch(type) {
    case MAP_IDALST: 
        loadFromIdaFile(path, loadSegment);
        break;
    case MAP_MSLINK:
        loadFromLinkFile(path, loadSegment);
        break;
    default:
        loadFromMapFile(path, loadSegment);
    }
    debug("Done, found "s + to_string(routines.size()) + " routines, " + to_string(vars.size()) + " variables");
    // create a bogus scan queue and populate the visited map with markers where the routines are 
    // for building the list of unclaimed blocks between them - these are lost when the map is saved to disk
    ScanQueue sq{Address{loadSegment, 0}, mapSize, {}};
    // mark all code locations
    for (const Routine &r : routines) {
        for (const Block &rb : r.reachable) sq.setRoutineIdx(rb.begin.toLinear(), rb.size(), VISITED_ID);
        for (const Block &ub : r.unreachable) sq.setRoutineIdx(ub.begin.toLinear(), ub.size(), VISITED_ID);
    }
    // rebuild the unclaimed blocks
    blocksFromQueue(sq, true);
    order();
}

Size CodeMap::routinesSize() const {
    Size ret = 0;
    for (const auto &r : routines) ret += r.size();
    return ret;
}

Routine CodeMap::getRoutine(const Address &addr) const {
    for (const Routine &r : routines) {
        if (r.extents.contains(addr)) return r;
        for (const Block &b : r.reachable) 
            if (b.contains(addr)) return r;
    }
    
    return {};
}

Routine CodeMap::getRoutine(const std::string &name) const {
    for (const Routine &r : routines)
        if (r.name == name) return r;
    return {};
}

Routine& CodeMap::getMutableRoutine(const std::string &name) {
    for (Routine &r : routines)
        if (r.name == name) return r;
    return routines.emplace_back(Routine(name, {}));
}

Variable CodeMap::getVariable(const std::string &name) const {
    auto it = std::find_if(vars.begin(), vars.end(), [&](const Variable &v){
        return v.name == name;
    });
    if (it != vars.end()) return *it;
    return Variable{"", {}};
}

Variable CodeMap::getVariable(const Address &addr) const {
    auto it = std::find_if(vars.begin(), vars.end(), [&](const Variable &v){
        return v.addr == addr;
    });
    if (it != vars.end()) return *it;
    return Variable{"", {}};
}

Routine CodeMap::findByEntrypoint(const Address &ep) const {
    for (const Routine &r : routines)
        if (r.entrypoint() == ep) return r;
    
    return {};
}

// given a block, go over all routines in the map and check if it does not colide (meaning cross over even partially) with any blocks claimed by those routines
Block CodeMap::findCollision(const Block &b) const {
    for (const Routine &r : routines) {
        for (const Block &rb : r.reachable) if (rb.intersects(b)) return rb;
        for (const Block &ub : r.unreachable) if (ub.intersects(b)) return ub;
    }
    return {};
}

// matches routines by extents only, limited use, mainly unit test for alignment with IDA
Size CodeMap::match(const CodeMap &other, const bool onlyEntry) const {
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
Routine CodeMap::colidesBlock(const Block &b) const {
    const auto &found = find_if(routines.begin(), routines.end(), [&b](const Routine &r){
        return r.colides(b);
    });
    if (found != routines.end()) return *found;
    else return {};
}

// utility function used when constructing from an instance of SearchQueue
void CodeMap::closeBlock(Block &b, const Address &next, const ScanQueue &sq, const bool unclaimedOnly) {
    if (!b.isValid() || next == b.begin) return;

    b.end = Address{next.toLinear() - 1};
    b.end.move(b.begin.segment);
    debug(next.toString() + ": closing block " + b.toString() + ", curId = " + to_string(curId) + ", prevId = " + to_string(prevId) 
        + ", curBlockId = " + to_string(curBlockId) + ", prevBlockId = " + to_string(prevBlockId));

    if (!b.isValid())
        throw AnalysisError("Attempted to close invalid block");

    // used when loading the map back from a file, we already know all the blocks for all the routines, just need to rebuild the list of the unclaimed ones
    if (unclaimedOnly) {
        if (curBlockId == NULL_ROUTINE) {
            debug("    block is unclaimed");
            unclaimed.push_back(b);
        }
        else {
            debug("    ignoring block");
        }
        prevBlockId = curBlockId;
        return;
    }

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

Block CodeMap::moveBlock(const Block &b, const Word segment) const {
    Block block(b);
    if (block.inSegment(segment)) {
        block.move(segment);
    }
    else {
        warn("Unable to move block "s + block.toString() + " to segment " + hexVal(segment));
    }
    return block;
}

void CodeMap::sort() {
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
    // sort segments and variables
    std::sort(segments.begin(), segments.end());
    std::sort(vars.begin(), vars.end());
}

// this is the string representation written to the mapfile, while Routine::toString() is the stdout representation for info/debugging
std::string CodeMap::routineString(const Routine &r, const Word reloc) const {
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

std::string CodeMap::varString(const Variable &v, const Word reloc) const {
    ostringstream str;
    if (!v.addr.isValid()) throw ArgError("Invalid variable address for '" + v.name + "' while converting to string");
    const Segment vseg = findSegment(v.addr.segment);
    if (vseg.type == Segment::SEG_NONE) throw AnalysisError("Unable to find segment for variable " + v.name + " / " + v.addr.toString());
    str << v.name << ": " << vseg.name << " VAR " << hexVal(v.addr.offset, false);
    return str.str();
}

void CodeMap::order() {
    debug("Recalculating routine extents and sorting map");
    // TODO: coalesce adjacent blocks, see routine_35 of hello.exe: 1415-14f7 R1412-1414 R1415-14f7
    for (auto &r : routines) 
        r.recalculateExtents();
    sort();
}

void CodeMap::save(const std::string &path, const Word reloc, const bool overwrite) const {
    if (empty()) return;
    if (checkFile(path).exists && !overwrite) throw AnalysisError("Map file already exists: " + path);
    info("Saving code map (routines = " + to_string(routineCount()) + ") to "s + path + ", reversing relocation by " + hexVal(reloc));
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
         << "# Discovered routines, one per line, syntax is \"RoutineName: Segment Type(NEAR/FAR) Extents [R/U]Block1 [R/U]Block2... [annotation1] [annotation2]...\"" << endl
         << "# The routine extents is the largest continuous block of instructions attributed to this routine and originating" << endl 
         << "# at the location determined to be the routine's entrypoint." << endl
         << "# Blocks are offset ranges relative to the segment that the routine belongs to, specifying address as belonging to the routine." << endl 
         << "# Blocks starting with R contain code that was determined reachable, U were unreachable but still likely belong to the routine." << endl
         << "# The routine blocks may cover a greater area than the extents if the routine has disconected chunks it jumps into." << endl
         << "# Possible annotation types:" << endl
         << "# ignore - ignore this routine in processing (comparison, signature extraction etc.)" << endl
         << "# complete - this routine was completely reconstructed into C, only influences stat display when printing map" << endl
         << "# external - is part of an external library (e.g. libc), ignore in comparison, don't count as uncompleted in stats" << endl
         << "# detached - routine has no callers, looks useless, don't count as uncompleted in stats" << endl
         << "# assembly - routine was written in assembly, don't include in comparisons by default" << endl
         << "# duplicate - routine is a duplicate of another" << endl
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
CodeMap::Summary CodeMap::getSummary(const bool verbose, const bool brief, const bool format) const {
    ostringstream str;
    Summary sum;
    if (empty()) {
        str << "--- Empty code map" << endl;
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
    str << "--- Map contains " << mapCount << " routines" << endl
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
                str << r.dump(verbose, true);
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

    if (vars.size()) str << "--- Map contains " << vars.size() << " variables" << endl;
    for (const auto &v : vars) {
        str << varString(v, 0) << endl;
    }

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

void CodeMap::setSegments(const std::vector<Segment> &seg) {
    segments = seg;
    std::sort(segments.begin(), segments.end());
}

void CodeMap::storeVariable(const Variable &v) {
    const Segment ds = findSegment(v.addr.segment);
    if (ds.type == Segment::SEG_NONE) {
        debug("Unable to save variable at address " + v.addr.toString() + ", no record of segment at " + hexVal(v.addr.segment));
        return;
    }
    if (v.name.empty()) {
        const size_t idx = vars.size() + 1;
        vars.emplace_back(Variable{"var_" + to_string(idx), v.addr});
    }
    else vars.push_back(v);
}

Size CodeMap::segmentCount(const Segment::Type type) const {
    Size ret = 0;
    for (const Segment &s : segments) {
        if (s.type == type) ret++;
    }
    return ret;
}

Segment CodeMap::findSegment(const Word addr) const {
    for (const auto &s : segments) {
        if (s.address == addr) return s;
    }
    return {};
}

Segment CodeMap::findSegment(const std::string &name) const {
    for (const auto &s : segments) {
        if (s.name == name) return s;
    }
    return {};
}

Segment CodeMap::findSegment(const Offset off, const bool past) const {
    Segment ret;
    // assume sorted segments, find last segment which contains the argument offset
    for (const auto &s : segments) {
        const Offset segOff = SEG_TO_OFFSET(s.address);
        // in this mode, find any segment that's past the argument offset
        if (past && segOff > off) return s;
        // otherwise, update the segment and continue
        if (segOff <= off && off - segOff <= OFFSET_MAX) ret = s;
    }
    return ret;
}

enum BlockType { BLOCK_NONE, BLOCK_EXTENTS, BLOCK_REACHABLE, BLOCK_UNREACHABLE };

void CodeMap::loadFromMapFile(const std::string &path, const Word reloc) {
    static const regex 
        RANGE_RE{"([0-9a-fA-F]{1,4})-([0-9a-fA-F]{1,4})"},
        SIZE_RE{"Size\\s+([0-9a-fA-F]+)"};
    debug("Loading code map from "s + path + ", relocating to " + hexVal(reloc));
    ifstream mapFile{path};
    string line;
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
            const string varname = match.str(1), segname = match.str(2), offstr = match.str(3), attr = match.str(4);
            Segment varseg;
            if ((varseg = findSegment(segname)).type == Segment::SEG_NONE) throw ParseError("Line " + to_string(lineno) + ": unknown segment '" + segname + "'");
            Address varaddr{varseg.address, static_cast<Word>(stoi(offstr, nullptr, 16))};
            Variable var{varname, varaddr};
            if (!attr.empty()) {
                istringstream sstr{attr};
                string attr_val;
                while (sstr >> attr_val) {
                    if (attr_val == "external") var.external = true;
                    else if (attr_val == "bss") var.bss = true;
                    else throw ParseError("Line " + to_string(lineno) + ": invalid variable attribute: '" + attr_val + "'");
                }
            }
            vars.push_back(var);
            continue;
        }
        // otherwise try interpreting as a routine description
        istringstream sstr{line};
        Routine r;
        Segment rseg;
        smatch match;
        int tokenno = 0;
        string token;
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
            r.idx = routines.size() + 1;
            routines.push_back(r);
        }
        else throw ParseError("Line " + to_string(lineno) + ": invalid routine extents " + r.extents.toString());
    } // iterate over mapfile lines

    if (mapSize == 0) throw ParseError("Invalid or undefined map size");
}

// construct code map from Microsoft LINK mapfile
void CodeMap::loadFromLinkFile(const std::string &path, const Word reloc) {
    debug("Loading code map from linker mapfile " + path + ", relocation factor " + hexVal(reloc));
    ifstream fstr{path};
    string line;
    Size lineno = 0;
    enum {
        LINKMAP_NONE,
        LINKMAP_SEGMENTS,
        LINKMAP_PUBLICS
    } mode = LINKMAP_NONE;
    static const string 
        HEXVAL_RE_STR{"([0-9A-F]+)"},
        NAME_RE_STR{"([_$0-9A-Za-z]+)"},
        SEGMENTS_RE_STR{"\\s*Start\\s+Stop\\s+Length\\s+Name\\s+Class"},
        PUBLICS_RE_STR{"\\s*Address\\s+Publics by Name"},
        PUBLVAL_RE_STR{"\\s*Address\\s+Publics by Value"},
        SEGDEF_RE_STR{"\\s*" + HEXVAL_RE_STR + "H\\s+" + HEXVAL_RE_STR + "H\\s+" + HEXVAL_RE_STR + "H\\s+" + NAME_RE_STR + "\\s+" + NAME_RE_STR},
        PUBDEF_RE_STR{"\\s*" + HEXVAL_RE_STR + ":" + HEXVAL_RE_STR + "\\s+" + NAME_RE_STR};
    static const regex SEGMENTS_RE{SEGMENTS_RE_STR}, PUBLICS_RE{PUBLICS_RE_STR}, PUBVAL_RE{PUBLVAL_RE_STR}, SEGDEF_RE{SEGDEF_RE_STR}, PUBDEF_RE{PUBDEF_RE_STR};
    vector<string> tokens;
    Size totalSize = 0;
    while (safeGetline(fstr, line)) {
        lineno++;
        // switch into segment parsing mode
        if (mode != LINKMAP_SEGMENTS && std::regex_match(line, SEGMENTS_RE)) {
            PARSE_DEBUG("Segment definitions starting on line " + to_string(lineno));
            mode = LINKMAP_SEGMENTS; 
            continue;
        }
        // switch into public parsing mode
        else if (mode != LINKMAP_PUBLICS && std::regex_match(line, PUBLICS_RE)) {
            PARSE_DEBUG("Public definitions starting on line " + to_string(lineno));
            mode = LINKMAP_PUBLICS; 
            continue;
        }
        // switch back to no mode, ignore public values
        else if (std::regex_match(line, PUBVAL_RE)) {
            PARSE_DEBUG("Public values starting on line " + to_string(lineno));
            mode = LINKMAP_NONE;
            continue;
        }
        // parse segment definition
        else if (mode == LINKMAP_SEGMENTS && (tokens = extractRegex(SEGDEF_RE, line)).size() == 5) {
            const Offset 
                start = std::stoi(tokens[0], nullptr, 16), 
                stop = std::stoi(tokens[1], nullptr, 16);
            if (start > stop) throw ParseError("Start offset above end offset for linkmap segment at line " + to_string(lineno));
            const Size 
                length = std::stoi(tokens[2]),
                size = stop - start;
            const string
                name = tokens[3],
                type = tokens[4];
            const Word segAddr = OFFSET_TO_SEG(start);
            PARSE_DEBUG("Segment definition on line " + to_string(lineno) + ": start " + hexVal(start) + " (addr " + hexVal(segAddr) + ")" ", stop " + hexVal(stop) + " (size " + hexVal(size) 
                + "), length " + hexVal(length) + " name '" + name + "', type '" + type + "'");
            if (stop > totalSize) totalSize = stop;
            const Segment existSeg = findSegment(segAddr);
            if (existSeg.type != Segment::SEG_NONE) {
                PARSE_DEBUG("Segment already exists at address " + hexVal(segAddr) + ": " + existSeg.toString());
                continue;
            }
            Segment::Type segType = Segment::SEG_NONE;
            if (type == "CODE") segType = Segment::SEG_CODE;
            else if (type.starts_with("DAT") || type == "BSS" || type == "CONST" || type == "MP" || type == "FAR_DATA" || type == "FAR_BSS") segType = Segment::SEG_DATA;
            else {
                PARSE_DEBUG("Ignoring segment of type '" + type + "'");
                continue;
            }
            segments.emplace_back(Segment{name, segType, segAddr});
        }
        // parse public definition
        else if (mode == LINKMAP_PUBLICS && (tokens = extractRegex(PUBDEF_RE, line)).size() == 3) {
            const Address addr{
                static_cast<Word>(std::stoi(tokens[0], nullptr, 16)), 
                static_cast<Word>(std::stoi(tokens[1], nullptr, 16))};
            const string name = tokens[2];
            PARSE_DEBUG("Public definition on line " + to_string(lineno) + ", addr " + addr.toString() + ", name '" + name + "'");
            Segment pubSeg = findSegment(addr.segment);
            if (pubSeg.type == Segment::SEG_NONE) {
                PARSE_DEBUG("Unable to find segment at addr " + hexVal(addr.segment) + " for public " + name + ", ignoring");
                continue;
            }
            else if (pubSeg.type == Segment::SEG_CODE) {
                PARSE_DEBUG("\tPublic belongs to code segment, attempting to register routine");
                const Routine existRoutine = getRoutine(addr);
                if (existRoutine.isValid()) {
                    PARSE_DEBUG("Routine already exists at " + addr.toString() + ": " + existRoutine.toString() + ", ignoring");
                    continue;
                }
                Routine r{name, Block{addr}};
                r.idx = routineCount() + 1;
                routines.push_back(r);
            }
            else if (pubSeg.type == Segment::SEG_DATA) {
                PARSE_DEBUG("\tPublic belongs to data segment, attempting to register variable");
                Variable existVar = getVariable(addr);
                if (existVar.addr.isValid()) {
                    PARSE_DEBUG("Variable already exists at " + addr.toString() + ": " + existVar.toString() + ", ignoring");
                    continue;
                }
                vars.emplace_back(Variable{name, addr});
            }
            else {
                PARSE_DEBUG("Ignoring public not in code or data segment");
                continue;
            }
        }
        // ignore everything else
    }
    mapSize = totalSize;
    debug("Finished parsing linker map file, map size: " + hexVal(mapSize) + ", segments: " + to_string(segments.size()));
}

// create code map from IDA listing (.lst) file
// TODO: add collision checks
void CodeMap::loadFromIdaFile(const std::string &path, const Word reloc) {
    static const string 
        NAME_RE_STR{"[_a-zA-Z0-9]+"},
        OFFSET_RE_STR{"[0-9a-fA-F]{1,4}"},
        ADDR_RE_STR{"(" + NAME_RE_STR + "):(" + OFFSET_RE_STR + ")"},
        LOAD_LEN_STR{"Loaded length: ([0-9a-fA-F]+)h"};
    const regex ADDR_RE{ADDR_RE_STR}, LOAD_LEN_RE{LOAD_LEN_STR};
    debug("Loading IDA code map from "s + path + ", relocation factor " + hexVal(reloc));
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
                PARSE_DEBUG("Extracted loaded length: " + hexVal(mapSize) + " from line " + to_string(lineno));
            }
            continue;
        }
        // split the address components
        const auto addrParts = extractRegex(ADDR_RE, addrStr);
        if (addrParts.size() != 2) throw ParseError("Unable to separate address components on line " + to_string(lineno));
        const string segName = addrParts.front(), offsetStr = addrParts.back();
        const Word offsetVal = static_cast<Word>(stoi(offsetStr, nullptr, 16));
        PARSE_DEBUG("Line " + to_string(lineno) + ": seg=" + segName + ", off=" + hexVal(offsetVal) + ", name='" + nameStr + "', pos=" + hexVal(globalPos));
        Address curAddr{curSegment.address, offsetVal};
        // the next token is going to determine the type of the line, e.g. proc/segment/var
        string typeStr;
        // ignore lines with no type discriminator, likely an asm directive or a standalone label
        if (!(sstr >> typeStr) || typeStr.empty()) continue;
        // force lowercase
        std::transform(typeStr.begin(), typeStr.end(), typeStr.begin(), [](unsigned char c){ 
            return std::tolower(c); 
        });
        PARSE_DEBUG("\ttype: '" + typeStr + "'");
        // segment start
        if (typeStr == "segment") {
            if (curSegment.type != Segment::SEG_NONE) throw ParseError("New segment opening while previous segment " + curSegment.name + " still open on line " + to_string(lineno));
            string alignStr, visStr, clsStr;
            if (!(sstr >> alignStr >> visStr >> clsStr)) throw ParseError("Invalid segment definition on line " + to_string(lineno));
            PARSE_DEBUG("\tsegment align=" + alignStr + ", vis=" + visStr + ", cls=" + clsStr);
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
            PARSE_DEBUG("\tinitialized new segment at address " + hexVal(curSegment.address) + ", globalPos=" + hexVal(globalPos));
            prevOffset = 0;
        }
        // segment end
        else if (typeStr == "ends") {
            PARSE_DEBUG("\tclosing segment " + curSegment.name);
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
            PARSE_DEBUG("Opened proc: " + curProc.toString());
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
