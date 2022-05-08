#ifndef DOSTRACE_H
#define DOSTRACE_H

#include <string>
#include <vector>
#include <memory>

#include "cpu.h"
#include "memory.h"
#include "dos.h"

struct VM {
    std::unique_ptr<Cpu> cpu;
    std::unique_ptr<Arena> memory;
    std::unique_ptr<Dos> os;
    VM();
    std::string info() const;
};

enum CmdStatus {
    CMD_OK, CMD_FAIL, CMD_UNKNOWN, CMD_EXIT
};

CmdStatus commandDispatch(VM &vm, const std::string &cmd);

#endif // DOSTRACE_H