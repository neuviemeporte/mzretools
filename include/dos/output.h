#ifndef OUTPUT_H
#define OUTPUT_H

#include <string>

enum LogModule {
    LOG_SYSTEM,
    LOG_CPU,
    LOG_MEMORY,
    LOG_OS,
    LOG_INTERRUPT,
    LOG_ANALYSIS,
    LOG_OTHER,
};

enum LogPriority {
    LOG_DEBUG,
    LOG_VERBOSE,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_SILENT,
};

void output(const std::string &msg, const LogModule mod, const LogPriority pri = LOG_INFO);
void setOutputLevel(const LogPriority minPriority);
void setModuleLevel(const LogModule mod, const LogPriority minPriority);

#endif // OUTPUT_H
