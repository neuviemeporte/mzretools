#include "dos/output.h"

#include <iostream>
#include <map>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <unistd.h>
#include <sstream>


using namespace std;

static LogPriority globalPriority = LOG_INFO;
static vector<string> msgBuffer;

struct ColorConfig {
    bool shouldColor;
    ColorConfig() : shouldColor(true) {
        if (isatty(STDOUT_FILENO) != 1 || getenv("NO_COLOR") != nullptr) shouldColor = false;
        if (getenv("FORCE_COLOR") != nullptr) shouldColor = true;
    }
} cc;

static map<LogModule, bool> moduleVisibility = {
    { LOG_SYSTEM,    true },
    { LOG_CPU,       true },
    { LOG_MEMORY,    true },
    { LOG_OS,        true },
    { LOG_INTERRUPT, true },
    { LOG_ANALYSIS,  true },
    { LOG_OTHER,     true },
};

void output(const std::string &msg, const LogModule mod, const LogPriority pri, const Color color, const bool suppressNewline) {
    if (pri == LOG_DEBUG && globalPriority > LOG_DEBUG) return;
    ostringstream ostr;
    const bool hide = (pri < globalPriority || !moduleVisibility[mod]);
    ostream& str = hide ? ostr : cout;
    if (color != OUT_DEFAULT) str << output_color(color);
    str << msg;
    if (color != OUT_DEFAULT) str << output_color(OUT_DEFAULT);
    if (!suppressNewline) str << "\n";
    if (hide) msgBuffer.push_back(ostr.str());
}

LogPriority getOutputLevel() {
    return globalPriority;
}

void setOutputLevel(const LogPriority minPriority) {
    globalPriority = minPriority;
}

void setModuleVisibility(const LogModule mod, const bool visible) {
    moduleVisibility[mod] = visible;
}

bool moduleVisible(const LogModule mod) {
    assert(mod < moduleVisibility.size());
    return moduleVisibility[mod];
}

string output_color(const Color c) {
    if (!cc.shouldColor) return {};
    string ret;
    switch (c) {
    case OUT_RED:
        ret = "\033[30;31m";
        break;
    case OUT_BRIGHTRED:
        ret = "\033[30;91m";
        break;
    case OUT_BRIGHTWHITE:
        ret = "\033[30;97m";
        break;
    case OUT_BRIGHTBLACK:
        ret = "\033[30;90m";
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
    case OUT_CYAN:
        ret = "\033[30;36m";
        break;
    default:
        ret = "\033[0m";
        break;
    }
    return ret;
}

void clearOutputBuffer() {
    msgBuffer.clear();
}

void flushOutputBuffer() {
    for (const auto &m : msgBuffer) cout << m;
    clearOutputBuffer();
}
