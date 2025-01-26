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

