#include "dos/output.h"

#include <iostream>
#include <map>

using namespace std;

static LogPriority globalPriority = LOG_INFO;
static map<LogModule, LogPriority> modulePriority = {
    { LOG_SYSTEM,    LOG_INFO },
    { LOG_CPU,       LOG_INFO },
    { LOG_MEMORY,    LOG_INFO },
    { LOG_OS,        LOG_INFO },
    { LOG_INTERRUPT, LOG_INFO },
    { LOG_ANALYSIS,  LOG_INFO },
    { LOG_OTHER,  LOG_INFO },
};

void output(const std::string &msg, const LogModule mod, const LogPriority pri, const bool suppressNewline) {
    if (pri < globalPriority || pri < modulePriority[mod]) return;
    cout << msg;
    if (!suppressNewline) cout << endl;
}

void setOutputLevel(const LogPriority minPriority) {
    globalPriority = minPriority;
}

void setModuleLevel(const LogModule mod, const LogPriority minPriority) {
    modulePriority[mod] = minPriority;
}