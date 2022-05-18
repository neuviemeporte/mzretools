#ifndef SYSTEM_H
#define SYSTEM_H

#include <string>
#include <vector>
#include <memory>

#include "cpu.h"
#include "memory.h"
#include "dos.h"
#include "interrupt.h"

enum CmdStatus {
    CMD_OK, CMD_FAIL, CMD_UNKNOWN, CMD_EXIT
};

class System {
private:
    std::unique_ptr<Cpu> cpu_;
    std::unique_ptr<Arena> memory_;
    std::unique_ptr<Dos> os_;
    std::unique_ptr<InterruptInterface> int_;
    SegmentedAddress loadedCode_, loadedStack_;

public:
    System();
    std::string info() const;
    CmdStatus commandDispatch(const std::string &cmd);

private:
    void printHelp();
    void cmdOutput(const std::string &msg);
    void cmdError(const std::string &verb, const std::string &message);
    CmdStatus loadCommand(const std::vector<std::string> &params);
    CmdStatus execCommand();
    CmdStatus dumpCommand(const std::vector<std::string> &params);
};

#endif // SYSTEM_H