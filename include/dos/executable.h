#ifndef EXECUTABLE_H
#define EXECUTABLE_H

#include <vector>
#include <set>

#include "dos/types.h"
#include "dos/address.h"
#include "dos/memory.h"
#include "dos/routine.h"
#include "dos/mz.h"

class Executable {
    // TODO: just keep the load module data, not the entire memory space
    const Memory code;
    Word loadSegment;
    // actually the load module size
    Size codeSize;
    Address ep, stack;
    Block codeExtents;
    std::vector<Segment> segments;

public:
    explicit Executable(const MzImage &mz);
    Executable(const Word loadSegment, const std::vector<Byte> &data);
    Address loadAddr() const { return { loadSegment, 0 }; }
    const Address& entrypoint() const { return ep; }
    const Address& stackAddr() const { return stack; }
    void setEntrypoint(const Address &addr);

    Size size() const { return codeSize; }
    Block extents() const { return codeExtents; }
    bool contains(const Address &addr) const { return codeExtents.contains(addr); }
    void storeSegment(const Segment::Type type, const Word addr);
    const Memory& getCode() const { return code; }
    const Byte* codePointer(const Address &addr) const { return code.pointer(addr); }
    const std::vector<Segment>& getSegments() const { return segments; }
    Word getLoadSegment() const { return loadSegment; }
    Address find(const ByteString &pattern, Block where = {}) const;

private:
    void init();
};

#endif // EXECUTABLE_H
