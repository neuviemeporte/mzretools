#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <string>
#include <vector>
#include "dos/types.h"
#include "dos/address.h"
#include "dos/mz.h"

static constexpr int NULL_ROUTINE = 0;

struct Routine {
    std::string name;
    int id;
    Block extents;
    std::vector<Block> chunks;
    size_t stackSize;
    Routine() : id(NULL_ROUTINE), stackSize(0) {}
    Routine(const std::string &name, const int id, const Block &extents) : name(name), id(id), extents(extents), stackSize(0) {}
    Address entrypoint() const { return extents.begin; }
    bool contains(const Address &addr) const;
    std::string toString() const;
};

struct RoutineMap {
    std::vector<Routine> routines;
    RoutineMap() {}
    RoutineMap(const std::string &path);
    bool empty() const { return routines.empty(); }
    void dump() const;
    Size match(const RoutineMap &other) const;
    void addBlock(const Block &b, const int id);
};

struct Executable {
    std::vector<Byte> code;
    Address entrypoint;
    Word reloc;
    explicit Executable(const MzImage &mz);
};

RoutineMap findRoutines(const Executable &exe);
bool compareCode(const Executable &base, const Executable &object);

#endif // ANALYSIS_H