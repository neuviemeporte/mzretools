#include <string>
#include <algorithm>
#include <sstream>
#include <regex>
#include <fstream>
#include <iostream>

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
    if (near) str << " [near]";
    else str << " [far]";
    if (ignore) str << " [ignored]";
    if (complete) str << " [complete]";
    if (unclaimed) str << " [unclaimed]";
    if (external) str << " [external]";
    if (assembly) str << " [assembly]";
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

RoutineMap::RoutineMap(const ScanQueue &sq, const std::vector<Segment> &segs, const Word loadSegment, const Size mapSize) : mapSize(mapSize) {
    const Size routineCount = sq.routineCount();
    if (routineCount == 0)
        throw AnalysisError("Attempted to create routine map from search queue with no routines");
    
    setSegments(segs);
    info("Building routine map from search queue contents: "s + to_string(routineCount) + " routines over " + to_string(segments.size()) + " segments");
    routines = sq.getRoutines();

    const Offset startOffset = SEG_TO_OFFSET(loadSegment);
    const Offset endOffset = startOffset + mapSize;
    Block b(startOffset);
    prevId = curBlockId = prevBlockId = NULL_ROUTINE;
    Segment curSeg;

    for (Offset mapOffset = startOffset; mapOffset < endOffset; ++mapOffset) {
        // find segment matching currently processed offset
        // TODO: add size field to Segment, calculate segment bounds in routine find routine, verify going out of segment range? In that case also don't need to check every iteration
        Segment offSeg = findSegment(mapOffset);
        if (offSeg.type == Segment::SEG_NONE) continue;
        if (offSeg != curSeg) {
            curSeg = offSeg;
            debug("=== Segment change to " + curSeg.toString());
        }
        // convert map offset to segmented address
        Address curAddr{mapOffset};
        curAddr.move(curSeg.address);   
        curId = sq.getRoutineId(mapOffset);
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
    closeBlock(b, endOffset, sq);

    // TODO: coalesce adjacent blocks, see routine_35 of hello.exe: 1415-14f7 R1412-1414 R1415-14f7

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

// recreate list of unclaimed blocks (lost after map is saved to file) from holes in the map coverage after loading the map back from a file
void RoutineMap::buildUnclaimed(const Word loadSegment) {
    if (mapSize == 0) return;
    Block b(loadSegment, 0);
    debug("Building unclaimed blocks, mapSize = " + sizeStr(mapSize) + ", load segment: " + hexVal(loadSegment));
    // need routines to be sorted
    sort();
    for (Size ri = 0; ri < size(); ++ri) {
        const Routine &r = getRoutine(ri);
        debug("Processing routine " + r.name + ": " + r.extents.toString());
        // the current routine starts after the last claimed position
        if (r.extents.begin > b.begin) {
            debug("Potential unclaimed block found starting at " + b.begin.toString());
            // check if the potential unclaimed block does not really belong to any routine,
            // it could be a detached chunk outside the main extents
            const auto chunkRoutine = getRoutine(b.begin);
            if (chunkRoutine.isValid()) {
                const auto chunkBlock = chunkRoutine.blockContaining(b.begin);
                // the found chunk could not encompass the entire potential unclaimed block,
                // so just advance the potential block's beginning past the chunk
                b.begin = chunkBlock.end + Offset(1);
                debug("Block belongs to chunk " + chunkBlock.toString() + " of routine " + chunkRoutine.name + ", advanced block to " + b.begin.toString());
            }
            // unclaimed block crosses segment boundary, split into two
            if (r.extents.begin.segment != b.begin.segment) {
                // first block ends before the segment start of the current routine
                b.end = Address(SEG_TO_OFFSET(r.extents.begin.segment) - 1);
                b.end.move(b.begin.segment);
                debug("Unclaimed block crosses segment boundary, forcing split: " + b.toString());
                if (b.isValid() && findSegment(b.begin.segment).type == Segment::SEG_CODE) 
                    unclaimed.push_back(b);
                // the second part of the unclaimed block starts at the segment start of the current routine, 
                // but make sure the routine doesn't start there as well
                if (r.extents.begin.offset > 0) {
                    Address newBegin = Address(r.extents.begin.segment, 0);
                    // make sure the new beginning is not before the original beginning 
                    // (routine from old segment could have spanned the current segment)
                    if (newBegin > b.begin) b.begin = newBegin;
                    else b.begin.move(r.extents.begin.segment);
                }
                else b.begin = {};
                debug("Unclaimed block in new segment starts at " + b.begin.toString());
            }
            // create unclaimed block between the last claimed position and the beginning of this routine
            b.end = r.extents.begin - 1;
            debug("Closing unclaimed block: " + b.toString());
            if (b.isValid() && findSegment(b.begin.segment).type == Segment::SEG_CODE) 
                unclaimed.push_back(b);
        }
        // start next potential unclaimed block past the end of the current routine
        b = Block(r.extents.end + Offset(1));
    }
    Address mapEnd{mapSize};
    // check last block, create unclaimed if it does not match the end of the load module
    if (b.begin < mapEnd) {
        b.end = mapEnd;
        b.end.move(b.begin.segment);
        debug("Last block before map end: " + b.toString());
        unclaimed.push_back(b);
    }
}

RoutineMap::RoutineMap(const std::string &path, const Word reloc) : RoutineMap() {
    static const regex LSTFILE_RE{".*\\.(lst|LST)"};
    const auto fstat = checkFile(path);
    if (!fstat.exists) throw ArgError("File does not exist: "s + path);
    smatch match;
    if (regex_match(path, match, LSTFILE_RE)) loadFromIdaFile(path, reloc);
    else loadFromMapFile(path, reloc);
    debug("Done, found "s + to_string(routines.size()) + " routines");
    // TODO: overridable value
    buildUnclaimed(0);
}

Routine RoutineMap::getRoutine(const Address &addr) const {
    for (const Routine &r : routines)
        for (const Block &b : r.reachable)
            if (b.contains(addr)) return r;
    
    return {};
}

Routine RoutineMap::findByEntrypoint(const Address &ep) const {
    for (const Routine &r : routines)
        if (r.entrypoint() == ep) return r;
    
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
void RoutineMap::closeBlock(Block &b, const Address &next, const ScanQueue &sq) {
    if (!b.isValid()) return;

    b.end = Address{next.toLinear() - 1};
    b.end.move(b.begin.segment);
    debug(next.toString() + ": closing block " + b.toString() + ", curId = " + to_string(curId) + ", prevId = " + to_string(prevId) 
        + ", curBlockId = " + to_string(curBlockId) + ", prevBlockId = " + to_string(prevBlockId));

    if (!b.isValid())
        throw AnalysisError("Attempted to close invalid block");

    // block contains reachable code
    if (curBlockId != NULL_ROUTINE) { 
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
    else if (prevBlockId != NULL_ROUTINE && curId == prevBlockId) { 
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
        assert(curBlockId == NULL_ROUTINE);
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
    rextent.rebase(reloc);
    if (rextent.begin.segment != rextent.end.segment) 
        throw AnalysisError("Beginning and end of extents of routine " + r.name + " lie in different segments: " + rextent.toString());
    Segment rseg = findSegment(r.extents.begin.segment);
    if (rseg.type == Segment::SEG_NONE) 
        throw AnalysisError("Unable to find segment for routine " + r.name + ", start addr " + r.extents.begin.toString() + ", relocated " + rextent.begin.toString());
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
    if (r.detached) str << " assembly";
    return str.str();
}

void RoutineMap::save(const std::string &path, const Word reloc, const bool overwrite) const {
    if (empty()) return;
    if (checkFile(path).exists && !overwrite) throw AnalysisError("Map file already exists: " + path);
    info("Saving routine map (size = " + to_string(size()) + ") to "s + path + ", reversing relocation by " + hexVal(reloc));
    ofstream file{path};
    for (auto s: segments) {
        s.address -= reloc;
        file << s.toString() << endl;
    }
    for (const auto &r : routines) {
        file << routineString(r, reloc) << endl;
    }
}

// TODO: implement a print mode of all blocks (reachable, unreachable, unclaimed) printed linearly, not grouped under routines
string RoutineMap::dump(const bool verbose, const bool brief, const bool format) const {
    ostringstream str;
    if (empty()) {
        str << "--- Empty routine map";
        return str.str();;
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
    Size mapCount = size();
    str << "--- Routine map containing " << mapCount << " routines" << endl
        << "Size " << sizeStr(mapSize) << endl;
    for (const auto &s : segments) {
        str << s.toString() << endl;
    }

    // display routines, gather statistics
    Size codeSize = 0, ignoredSize = 0, completedSize = 0, unclaimedSize = 0, externalSize = 0, dataCodeSize = 0, detachedSize = 0, assemblySize = 0;
    Size ignoreCount = 0, completeCount = 0, unclaimedCount = 0, externalCount = 0, dataCodeCount = 0, detachedCount = 0, assemblyCount = 0;
    for (const auto &r : printRoutines) {
        const auto seg = findSegment(r.extents.begin.segment);
        if (seg.type == Segment::SEG_CODE) {
            codeSize += r.size();
            // TODO: f15 does not have meaningful routines in the data segment other than the jump trampolines, 
            // but what about other projects?
            if (r.ignore) { ignoredSize += r.size(); ignoreCount++; }
            if (r.complete) { completedSize += r.size(); completeCount++; }
            if (r.unclaimed) { unclaimedSize += r.size(); unclaimedCount++; }
            if (r.external) { externalSize += r.size(); externalCount++; }
            if (r.detached) { detachedSize += r.size(); detachedCount++; }
            if (r.assembly) { assemblySize += r.size(); assemblyCount++; }
        }
        else if (!r.unclaimed) {
            dataCodeSize += r.size();
            dataCodeCount++;
        }
        // print routine unless hide mode enabled and it's not important - show only uncompleted routines and big enough unclaimed blocks within code segments
        if (!(brief && (r.ignore || r.complete || r.external || r.assembly || r.size() < 3 || seg.type != Segment::SEG_CODE))) {
            if (!format) {
                str << r.toString(verbose);
                if (seg.type == Segment::SEG_DATA) str << " [data]";
                str << endl;
            }
            else {
                str << routineString(r, 0) << endl;
            }
        }
    }

    // print statistics
    const Size 
        dataSize = mapSize - codeSize,
        otherSize = ignoredSize - externalSize,
        otherCount = ignoreCount - externalCount,
        ignoredReachableSize = otherSize - detachedSize,
        ignoredReachableCount = otherCount - detachedCount,
        uncompleteSize = codeSize - (completedSize + ignoredSize + assemblySize + unclaimedSize),
        uncompleteCount = mapCount - (completeCount + ignoreCount + dataCodeCount + assemblyCount),
        unaccountedSize = codeSize - (completedSize + uncompleteSize + assemblySize + externalSize + ignoredReachableSize + detachedSize + unclaimedSize),
        unaccountedCount = mapCount - (completeCount + uncompleteCount + assemblyCount + externalCount + ignoredReachableCount + detachedCount + dataCodeCount);
    str << "--- Summary:" << endl
        << "Code size: " << sizeStr(codeSize) << " (" << ratioStr(codeSize, mapSize) << " of load module)" << endl
        << "  Completed: " << sizeStr(completedSize) << " (" << completeCount << " routines, " << ratioStr(completedSize, codeSize) << " of code) - 1:1 rewritten to high level language" << endl
        << "  Uncompleted: " << sizeStr(uncompleteSize) << " (" << uncompleteCount << " routines, " << ratioStr(uncompleteSize, codeSize) << " of code) - routines not yet rewritten which can be" << endl
        << "  Assembly: " << sizeStr(assemblySize) << " (" << assemblyCount << " routines, " << ratioStr(assemblySize, codeSize) << " of code) - impossible to rewrite 1:1" << endl
        << "  Ignored: " << sizeStr(ignoredSize) << " (" << ignoreCount << " routines, " << ratioStr(ignoredSize, codeSize) << " of code) - excluded from comparison" << endl
        << "    External: " << sizeStr(externalSize) << " (" << externalCount << " routines, " << ratioStr(externalSize, ignoredSize) << " of ignored) - e.g. libc library code" << endl
        << "    Other: " << sizeStr(otherSize) << " (" << otherCount << " routines, " << ratioStr(otherSize, ignoredSize) << " of ignored) - code ignored for other reasons" << endl
        << "      Reachable: " << sizeStr(ignoredReachableSize) << " (" << ignoredReachableCount << " routines, " << ratioStr(ignoredReachableSize, otherSize) << " of other) - code which has callers" << endl        
        << "      Unreachable: " << sizeStr(detachedSize) << " (" << detachedCount << " routines, " << ratioStr(detachedSize, otherSize) << " of other) - code which appears unreachable" << endl
        << "  Unclaimed: " << sizeStr(unclaimedSize) << " (" << unclaimedCount << " blocks, " << ratioStr(unclaimedSize, codeSize) << " of code) - holes between routines not covered by map" << endl
        << "  Unaccounted: " << sizeStr(unaccountedSize) << " (" << unaccountedCount << " routines) - consistency check, should be zero" << endl
        << "Data size: " << sizeStr(dataSize) << " (" <<ratioStr(dataSize, mapSize) << " of load module)" << endl
        << "  Routines in data segment: " << sizeStr(dataCodeSize) << ", " << dataCodeCount << " routines" << endl;
    return str.str();
}

void RoutineMap::setSegments(const std::vector<Segment> &seg) {
    segments = seg;
    std::sort(segments.begin(), segments.end());
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
        // ignore comments
        if (line[0] == '#') continue;
        // try to interpret as code size
        else if (regex_match(line, match, SIZE_RE)) {
            mapSize = std::stoi(match.str(1), nullptr, 16);
            continue;
        }
        // try to interpret as a segment
        else if (!(match = Segment::stringMatch(line)).empty()) {
            Segment s(match);
            s.address += reloc;
            debug("Loaded segment: " + s.toString());
            segments.push_back(s);
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
                throw ParseError("Line "s + to_string(lineno) + ": block " + block.toString() + " colides with routine " + colideRoutine.toString(false));
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
            debug("routine: "s + r.toString());
            routines.push_back(r);
        }
    } // iterate over mapfile lines
}

// create routine map from IDA .lst file
// TODO: add collision checks
void RoutineMap::loadFromIdaFile(const std::string &path, const Word reloc) {
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
