#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <string>
#include <vector>
#include "dos/types.h"
#include "dos/address.h"
#include "dos/registers.h"
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
    bool colides(const Block &block) const;
    std::string toString(const bool showChunks = true) const;
};

class RoutineMap {
    friend class SystemTest;
    std::vector<Routine> routines;

public:
    RoutineMap() {}
    RoutineMap(const std::string &path);

    Size size() const { return routines.size(); }
    bool empty() const { return routines.empty(); }
    Size match(const RoutineMap &other) const;
    void addBlock(const Block &b, const int id);
    bool colidesBlock(const Block &b) const;
    void sort();
    void save(const std::string &path) const;
    void dump() const;

private:
    void loadFromMapFile(const std::string &path);
    void loadFromIdaFile(const std::string &path);
};

struct Executable {
    std::vector<Byte> code;
    Address entrypoint;
    Address stack;
    Word reloc;
    explicit Executable(const MzImage &mz);
};

class RegisterState {
private:
    static constexpr Word WORD_KNOWN = 0xffff;
    static constexpr Word BYTE_KNOWN = 0xff;
    Registers regs_;
    Registers known_;

public:
    RegisterState();
    bool isKnown(const Register r) const;
    Word getValue(const Register r) const;
    void setValue(const Register r, const Word value);
    void setUnknown(const Register r);
    std::string regString(const Register r) const;
    std::string toString() const;

private:
    void setState(const Register r, const Word value, const bool known);
    std::string stateString(const Register r) const;
};

RoutineMap findRoutines(const Executable &exe);
bool compareCode(const Executable &base, const Executable &object);

#endif // ANALYSIS_H