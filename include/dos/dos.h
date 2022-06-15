#ifndef DOS_H
#define DOS_H

#include <string>
#include <ostream>
#include "dos/types.h"
#include "dos/address.h"
#include "dos/mz.h"

class Memory;

struct LoadModule {
    Address code, stack;
    Size size;
};

class Dos {
private:
    Memory* memory_;

public:
    Dos(Memory *memory);
    std::string name() const { return "NinjaDOS 1.0"; };
    LoadModule loadExe(MzImage &mz);
    int version() const { return 2; }
};

#endif // SYSCALL_H
