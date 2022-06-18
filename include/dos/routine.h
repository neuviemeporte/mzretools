#ifndef ROUTINE_H
#define ROUTINE_H

#include <string>
#include <vector>
#include "dos/types.h"
#include "dos/address.h"

struct Routine {
    std::string name;
    int id;
    Block extents;
    std::vector<Block> chunks;
    Routine(const std::string &name, const int id, const Address &entrypoint) : name(name), id(id) {
        extents.begin = extents.end = entrypoint;
    }
    Address entrypoint() const { return extents.begin; }
    bool contains(const Address &addr) const;
};

struct Frame {
    Address address;
    int id;
    Frame() : id(0) {}
    Frame(const Address address, const int id) : address(address), id(id) {}
    bool isNull() const { return address.isNull(); }
};

#endif // ROUTINE_H