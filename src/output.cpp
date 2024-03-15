#include "dos/output.h"

#include <iostream>
#include <map>

using namespace std;

static LogPriority globalPriority = LOG_INFO;

static map<LogModule, bool> moduleVisible = {
    { LOG_SYSTEM,    true },
    { LOG_CPU,       true },
    { LOG_MEMORY,    true },
    { LOG_OS,        true },
    { LOG_INTERRUPT, true },
    { LOG_ANALYSIS,  true },
    { LOG_OTHER,     true },
};

void output(const std::string &msg, const LogModule mod, const LogPriority pri, const bool suppressNewline) {
    if (pri < globalPriority || !moduleVisible[mod]) return;
    cout << msg;
    if (!suppressNewline) cout << endl;
}

void setOutputLevel(const LogPriority minPriority) {
    globalPriority = minPriority;
}

void setModuleVisibility(const LogModule mod, const bool visible) {
    moduleVisible[mod] = visible;
}

string output_color(const Color c) {
    string ret;
    switch (c) {
    case OUT_RED:
        ret = "\033[30;31m";
        break;
    case OUT_BRIGHTRED:
        ret = "\033[30;91m";
        break;        
    case OUT_YELLOW:
        ret = "\033[30;33m";
        break;
    case OUT_BLUE:
        ret = "\033[30;94m";
        break;
    case OUT_GREEN:
        ret = "\033[30;32m";
        break;
    default:
        ret = "\033[0m";
        break;
    }
    return ret;
}