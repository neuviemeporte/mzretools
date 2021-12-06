#ifndef DOSTRACE_H
#define DOSTRACE_H

#include <string>
#include <vector>
#include <memory>

#include "cpu.h"
#include "memory.h"
#include "syscall.h"

struct VM {
    std::unique_ptr<Cpu> cpu;
    std::unique_ptr<Arena> memory;
    std::unique_ptr<Dos> os;
    VM();
};

enum CmdStatus {
    CMD_OK, CMD_FAIL, CMD_UNKNOWN, CMD_EXIT
};

CmdStatus loadCommand(VM &vm, const std::vector<std::string> &params);
CmdStatus dumpCommand(VM &vm, const std::vector<std::string> &params);
CmdStatus execCommand(VM &vm, const std::string &cmd);

#endif // DOSTRACE_H