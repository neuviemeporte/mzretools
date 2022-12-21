#include "dos/output.h"

#include <iostream>
#include <map>

using namespace std;

static LogPriority globalPriority = LOG_INFO;

void output(const std::string &msg, const LogModule mod, const LogPriority pri, const bool suppressNewline) {
    if (pri < globalPriority) return;
    cout << msg;
    if (!suppressNewline) cout << endl;
}

void setOutputLevel(const LogPriority minPriority) {
    globalPriority = minPriority;
}
