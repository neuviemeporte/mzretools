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

// dictionary of equivalent instruction sequences for variant-enabled comparison
// TODO: support user-supplied equivalence dictionary 
// TODO: do not hardcode, place in text file
// TODO: automatic reverse match generation
// TODO: replace strings with Instruction-s, support more flexible matching?
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
    codeExtents = Block{{loadSegment, Word(0)}, Address(SEG_TO_OFFSET(loadSegment) + codeSize - 1)};
    stack.relocate(loadSegment);
    storeSegment(Segment::SEG_STACK, stack.segment);
    debug("Loaded executable data into memory, code at "s + codeExtents.toString() + ", relocated entrypoint " + entrypoint().toString() + ", stack " + stack.toString());    
}

void Executable::setEntrypoint(const Address &addr) {
    ep = addr;
    ep.relocate(loadSegment);
}

void Executable::storeSegment(const Segment::Type type, const Word addr) {
    // ignore segments which are already known
    auto found = std::find_if(segments.begin(), segments.end(), [=](const Segment &s){
        return s.type == type && s.address == addr;
    });
    if (found != segments.end()) return;
    // check if an existing segment of a different type has the same address
    found = std::find_if(segments.begin(), segments.end(), [&](const Segment &s){ 
        return s.address == addr;
    });
    if (found != segments.end()) {
        const Segment &existing = *found;
        // existing code segments cannot share an address with another segment
        if (existing.type == Segment::SEG_CODE) return;
        // a new data segment trumps an existing stack segment
        else if (existing.type == Segment::SEG_STACK && type == Segment::SEG_DATA) {
            segments.erase(found);
        }
        else return;
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
    debug("Found new segment: " + seg.toString());
    segments.push_back(seg);
}




