#include "dos/scanq.h"
#include "dos/output.h"
#include "dos/util.h"
#include "dos/error.h"

#include <fstream>

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

string Destination::toString() const {
    ostringstream str;
    str << "[" << address.toString() << " / " << routineIdx << " / " << (isCall ? "call" : "jump") << "]";
    return str.str();
}

string Branch::toString() const {
    ostringstream str;
    str << source.toString() << " -> " << destination.toString() << " [";
    if (isCall) str << "call";
    else str << "jump";
    if (isConditional) str << ",cond";
    else str << ",nocond";
    if (isNear) str << ",near]";
    else str << ",far]";
    return str.str();
}

ScanQueue::ScanQueue(const Address &origin, const Size codeSize, const Destination &seed, const std::string name) :
    visited(codeSize, NULL_ROUTINE),
    origin(origin)
{
    debug("Initializing queue, origin: " + origin.toString() + ", size = " + to_string(codeSize) + ", seed: " + seed.toString() + ", name: '" + name + "'");
    queue.push_front(seed);
    RoutineEntrypoint ep{seed.address, seed.routineIdx, true};
    if (!name.empty()) ep.name = name;
    entrypoints.push_back(ep);
}

string ScanQueue::statusString() const { 
    return "[r"s + to_string(curSearch.routineIdx) + "/q" + to_string(size()) + "]"; 
} 

RoutineIdx ScanQueue::getRoutineIdx(Offset off) const {
    assert(off >= origin.toLinear());
    off -= origin.toLinear();
    assert(off < visited.size());
    return visited.at(off); 
}

void ScanQueue::setRoutineIdx(Offset off, const Size length, RoutineIdx idx) {
    if (idx == NULL_ROUTINE) idx = curSearch.routineIdx;
    if (off < origin.toLinear()) throw ArgError("Unable to mark visited location at offset " + hexVal(off) + " before origin: " + origin.toString());
    off -= origin.toLinear();
    if (off >= visited.size() || off + length > visited.size()) 
        throw ArgError("Unable to mark visited location at offset " + hexVal(off) + " with length " + sizeStr(length) + " past array of size " + hexVal(visited.size()));
    fill(visited.begin() + off, visited.begin() + off + length, idx);
}

void ScanQueue::clearRoutineIdx(Offset off) {
    assert(off >= origin.toLinear());
    off -= origin.toLinear();
    assert(off < visited.size());
    auto it = visited.begin() + off;
    const auto clearId = *it;
    if (clearId == NULL_ROUTINE) return;
    while (it != visited.end() && *it == clearId) {
        *it = NULL_ROUTINE;
        ++it;
    }
}

Destination ScanQueue::nextPoint() {
    if (!empty()) {
        curSearch = queue.front();
        queue.pop_front();
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

RoutineIdx ScanQueue::isEntrypoint(const Address &addr) const {
    const auto &found = std::find(entrypoints.begin(), entrypoints.end(), addr);
    if (found != entrypoints.end()) return found->idx;
    else return NULL_ROUTINE;
}

RoutineEntrypoint ScanQueue::getEntrypoint(const std::string &name) {
    const auto &found = std::find_if(entrypoints.begin(), entrypoints.end(), [&](const RoutineEntrypoint &ep){
        return ep.name == name;
    });
    if (found != entrypoints.end()) return *found;
    return {};
}

// return the set of routines found by the queue, these will only have the entrypoint set and an automatic name generated
vector<Routine> ScanQueue::getRoutines() const {
    auto routines = vector<Routine>{routineCount()};
    for (const auto &ep : entrypoints) {
        auto &r = routines.at(ep.idx - 1);
        // initialize routine extents with entrypoint address
        r.extents = Block(ep.addr);
        r.near = ep.near;
        if (!ep.name.empty()) r.name = ep.name;
        // assign automatic names to routines
        else if (ep.addr == originAddress()) r.name = "start";
        else r.name = "routine_"s + to_string(ep.idx);
    }
    return routines;
}

// TODO: proper segments
vector<Block> ScanQueue::getUnvisited() const {
    vector<Block> ret;
    Offset off = 0;
    Block curBlock;
    // iterate over contents of visited map
    for (const RoutineIdx idx : visited) {
        if (idx == NULL_ROUTINE && !curBlock.begin.isValid()) { 
            // switching to unvisited, open new block
            curBlock.begin = Address{off};
        }
        else if (idx != NULL_ROUTINE && curBlock.begin.isValid()) { 
            // switching to visited, close current block if open
            curBlock.end = Address{off - 1};
            curBlock.relocate(origin.segment);
            ret.push_back(curBlock);
            curBlock = Block();
        }
        off += 1;
    }
    return ret;
}

// function call, create new routine at destination if either not visited, 
// or visited but destination was not yet discovered as a routine entrypoint and this call now takes precedence and will replace it
bool ScanQueue::saveCall(const Address &dest, const RegisterState &regs, const bool near, const std::string name) {
    if (!dest.isValid()) return false;
    RoutineIdx destId = isEntrypoint(dest);
    if (destId != NULL_ROUTINE) 
        debug("Address "s + dest.toString() + " already registered as entrypoint for routine " + to_string(destId));
    else if (hasPoint(dest, true)) 
        debug("Search queue already contains call to address "s + dest.toString());
    else { // not a known entrypoint and not yet in queue
        destId = getRoutineIdx(dest.toLinear());
        RoutineIdx newRoutineIdx = routineCount() + 1;
        queue.emplace_back(Destination(dest, newRoutineIdx, true, regs));
        if (destId == NULL_ROUTINE)
            debug("Call destination not belonging to any routine, claiming as entrypoint for new routine " + to_string(newRoutineIdx) + ", queue size = " + to_string(size()));
        else 
            debug("Call destination belonging to routine " + to_string(destId) + ", reclaiming as entrypoint for new routine " + to_string(newRoutineIdx) + ", queue size = " + to_string(size()));
        RoutineEntrypoint ep{dest, newRoutineIdx, near};
        if (!name.empty()) ep.name = name;
        entrypoints.push_back(ep);
        return true;
    }
    return false;
}

// conditional jump, save as destination to be investigated, belonging to current routine
bool ScanQueue::saveJump(const Address &dest, const RegisterState &regs) {
    const RoutineIdx 
        curIdx = curSearch.routineIdx,
        destIdx = getRoutineIdx(dest.toLinear());
    if (destIdx != NULL_ROUTINE) 
        debug("Jump destination already visited from routine "s + to_string(destIdx));
    else if (hasPoint(dest, false))
        debug("Queue already contains jump to address "s + dest.toString());
    else { // not claimed by any routine and not yet in queue
        Address destCopy{dest};
        assert(curIdx <= entrypoints.size());
        const RoutineEntrypoint ep = entrypoints[curIdx - 1];
        if (ep.addr.segment != destCopy.segment) try {
            destCopy.move(ep.addr.segment);
        }
        catch(Error &e) {
            debug("Unable to move jump destination " + destCopy.toString() + " to segment of routine " + ep.toString() + ", ignoring");
            return false;
        }
        queue.emplace_front(Destination(destCopy, curSearch.routineIdx, false, regs));
        debug("Jump destination not yet visited, scheduled visit from routine " + to_string(curSearch.routineIdx) + ", queue size = " + to_string(size()));
        return true;
    }
    return false;
}

bool ScanQueue::saveBranch(const Branch &branch, const RegisterState &regs, const Block &codeExtents) {
    if (!branch.destination.isValid())
        return false;

    if (codeExtents.contains(branch.destination)) {
        bool ret;
        if (branch.isCall) 
            ret = saveCall(branch.destination, regs, branch.isNear); 
        else 
            ret = saveJump(branch.destination, regs);
        return ret;
    }
    else {
        debug(branch.source.toString() + ": Branch destination outside code boundaries: " + branch.destination.toString());
    }
    return false; 
}

// This is very slow!
void ScanQueue::dumpVisited(const string &path) const {
    // dump map to file for debugging
    ofstream mapFile(path);
    mapFile << "      ";
    for (int i = 0; i < 16; ++i) mapFile << hex << setw(5) << setfill(' ') << i << " ";
    mapFile << endl;
    const Offset start = origin.toLinear();
    const Size size = visited.size();
    info("DEBUG: Dumping visited map of size "s + hexVal(size) + " starting at " + hexVal(start) + " to " + path);
    for (Offset mapOffset = start; mapOffset < start + size; ++mapOffset) {
        const auto id = getRoutineIdx(mapOffset);
        //debug(hexVal(mapOffset) + ": " + to_string(m));
        const Offset printOffset = mapOffset - start;
        if (printOffset % 16 == 0) {
            if (printOffset != 0) mapFile << endl;
            mapFile << hex << setw(5) << setfill('0') << printOffset << " ";
        }
        switch (id) {
        // case BAD_ROUTINE: mapFile << " !!! "; break;
        // case NULL_ROUTINE: mapFile << "     "; break;
        default: mapFile << dec << setw(5) << setfill(' ') << id << " "; break;
        }
    }
}

void ScanQueue::dumpEntrypoints() const {
    debug("Scan queue contains " + to_string(entrypoints.size()) + " entrypoints");
    for (const auto &ep : entrypoints) {
        debug(ep.toString());
    }
}