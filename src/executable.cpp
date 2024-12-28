#include <map>
#include <vector>
#include <algorithm>
#include <regex>

#include "dos/executable.h"
#include "dos/analysis.h"
#include "dos/output.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

OUTPUT_CONF(LOG_ANALYSIS)

Executable::Executable(const MzImage &mz) : 
    code(mz.loadSegment(), mz.loadModuleData(), mz.loadModuleSize()),
    loadSegment(mz.loadSegment()),
    codeSize(mz.loadModuleSize()),
    stack(mz.stackPointer())
{
    // relocate entrypoint
    setEntrypoint(mz.entrypoint());
    init();
}

Executable::Executable(const Word loadSegment, const std::vector<Byte> &data) :
    code(loadSegment, data.data(), data.size()),
    loadSegment(loadSegment),
    codeSize(data.size()),
    stack{}
{
    setEntrypoint({0, 0});
    init();
}

// common initialization after construction
void Executable::init() {
    if (codeSize == 0)
        throw ArgError("Code size is zero while constructing executable");
    codeExtents = Block{{loadSegment, Word(0)}, Address(SEG_TO_OFFSET(loadSegment) + codeSize - 1)};
    storeSegment(Segment::SEG_CODE, ep.segment);
    stack.relocate(loadSegment);
    storeSegment(Segment::SEG_STACK, stack.segment);
    debug("Loaded executable data into memory, code at "s + codeExtents.toString() + ", relocated entrypoint " + entrypoint().toString() + ", stack " + stack.toString());
}

void Executable::setEntrypoint(const Address &addr, const bool relocate) {
    ep = addr;
    if (relocate) ep.relocate(loadSegment);
    debug("Entrypoint set to " + ep.toString());
}

Segment Executable::getSegment(const Word addr) const {
    Segment ret;
    auto found = std::find_if(segments.begin(), segments.end(), [=](const Segment &s){
        return s.address == addr;
    });
    if (found != segments.end()) ret = *found;
    return ret;
}

bool Executable::storeSegment(const Segment::Type type, const Word addr) {
    debug("Attempting to register segment " + hexVal(addr) + " of type " + Segment::typeString(type));
    const Address segAddress{addr, 0};
    if (!codeExtents.contains(segAddress)) {
        debug("Segment " + segAddress.toString() + " outside extents of executable: " + codeExtents.toString());
        return false;
    }
    // ignore segments which are already known
    auto found = std::find_if(segments.begin(), segments.end(), [=](const Segment &s){
        return s.type == type && s.address == addr;
    });
    if (found != segments.end()) {
        debug("Segment already registered");
        return true;
    }
    // check if an existing segment of a different type has the same address
    found = std::find_if(segments.begin(), segments.end(), [&](const Segment &s){ 
        return s.address == addr;
    });
    if (found != segments.end()) {
        const Segment &existing = *found;
        // existing code segments cannot share an address with another segment
        if (existing.type == Segment::SEG_CODE) {
            debug("Segment " + hexVal(addr) + " already exists with type CODE, ignoring");
            return false;
        }
        // a new data segment trumps an existing stack segment
        else if (existing.type == Segment::SEG_STACK && type == Segment::SEG_DATA) {
            debug("Segment " + hexVal(addr) + " already exists with type STACK, replacing with DATA");
            segments.erase(found);
        }
        else {
            debug("Segment " + hexVal(addr) + " already exists with type " + Segment::typeString(existing.type) + ", ignoring");
            return false;
        }
    }
    // compose name for new segment
    int idx=1;
    for (const auto &s : segments) {
        if (s.type == type) idx++;
    }
    string segName;
    switch (type) {
    case Segment::SEG_CODE: segName = "Code"; break;
    case Segment::SEG_DATA: segName = "Data"; break;
    case Segment::SEG_STACK: segName = "Stack"; break;
    default: throw AnalysisError("Unsupported segment type");
    }
    segName += to_string(idx);
    Segment seg{segName, type, addr};
    debug("Registered new segment: " + seg.toString());
    segments.push_back(seg);
    return true;
}

Address Executable::find(const ByteString &pattern, Block where) const {
    if (!where.isValid()) where = { 
        Address{loadSegment, 0}, 
        Address{SEG_TO_OFFSET(loadSegment) + codeSize}
    };
    return code.find(pattern, where);
}


