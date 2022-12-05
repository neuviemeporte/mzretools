#ifndef SYSTEM_H
#define SYSTEM_H

#include <string>
#include <vector>
#include <memory>

#include "dos/cpu.h"
#include "dos/memory.h"
#include "dos/dos.h"
#include "dos/interrupt.h"

enum CmdStatus {
    CMD_OK, CMD_FAIL, CMD_UNKNOWN, CMD_EXIT
};

class System {
    friend class SystemTest;

private:
    std::unique_ptr<Cpu> cpu_;
    std::unique_ptr<Memory> mem_;
    std::unique_ptr<Dos> os_;
    std::unique_ptr<InterruptInterface> int_;
    Analysis analysis_;

public:
    System();
    std::string systemInfo() const;
    std::string cpuInfo() const { return cpu_->info(); }
    CmdStatus command(const std::string &cmd);

private:
    using Params = std::vector<std::string>;
    void printHelp();
    void info(const std::string &msg);
    void error(const std::string &verb, const std::string &message);
    CmdStatus commandLoad(const std::vector<std::string> &params);
    CmdStatus commandRun(const Params &params);
    CmdStatus commandAnalyze();
    CmdStatus commandDump(const Params &params);
};

#endif // SYSTEM_H