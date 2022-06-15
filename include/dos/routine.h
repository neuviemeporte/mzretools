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
    bool contains(const Address &addr) const;
};

#endif // ROUTINE_H