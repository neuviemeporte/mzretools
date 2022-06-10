#ifndef ROUTINE_H
#define ROUTINE_H

#include <string>
#include <vector>
#include "dos/types.h"
#include "dos/address.h"

struct Routine {
    std::string name;
    std::vector<Block> chunks;
    Routine(const std::string &name) : name(name) {}
};

#endif // ROUTINE_H