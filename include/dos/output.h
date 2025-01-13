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

enum Color {
    OUT_DEFAULT,
    OUT_RED,
    OUT_YELLOW,
    OUT_BLUE,
    OUT_GREEN,
    OUT_BRIGHTRED,
};

void output(const std::string &msg, const LogModule mod, const LogPriority pri = LOG_INFO, const bool suppressNewline = false);
void setOutputLevel(const LogPriority minPriority);
void setModuleVisibility(const LogModule mod, const bool visible);
std::string output_color(const Color c);

// create output functions for a system module
#define OUTPUT_CONF(module) \
static void debug(const std::string &msg) {\
    output(msg, module, LOG_DEBUG);\
}\
static void verbose(const std::string &msg) {\
    output(msg, module, LOG_VERBOSE);\
}\
static void info(const std::string &msg) {\
    output(msg, module, LOG_INFO);\
}\
static void error(const std::string &msg) {\
    output("ERROR: "s + msg, module, LOG_ERROR);\
}\
static void warn(const std::string &msg) {\
    output("WARNING: "s + msg, module, LOG_WARN);\
}

extern const std::string VERSION;

#endif // OUTPUT_H
