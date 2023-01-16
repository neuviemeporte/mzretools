#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <string>
#include <vector>
#include "dos/types.h"
#include "dos/address.h"
#include "dos/mz.h"

struct Routine {
    std::string name;
    int id;
    Block extents;
    std::vector<Block> chunks;
    size_t stackSize;
    Routine(const std::string &name, const int id, const Address &entrypoint) : name(name), id(id), stackSize(0) {
        extents.begin = extents.end = entrypoint;
    }
    Address entrypoint() const { return extents.begin; }
    bool contains(const Address &addr) const;
    std::string toString() const;
};

struct RoutinePair {
    Routine r1, r2;
    RoutinePair(const std::string &name, const int id, const Address &e1, const Address &e2) : r1{name, id, e1}, r2{name, id, e2} {}
    std::string toString() const;
};

struct Frame {
    Address address;
    int id;
    Frame() : id(0) {}
    Frame(const Address address, const int id) : address(address), id(id) {}
    bool isNull() const { return address.isNull(); }
};

struct RoutineMap {
    bool success;
    std::vector<Routine> routines;
    RoutineMap() : success(false) {}
    RoutineMap(const std::string &path);
    void dump() const;
    Size match(const RoutineMap &other) const;
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