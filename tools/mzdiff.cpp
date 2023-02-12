#include "dos/util.h"
#include "dos/mz.h"
#include "dos/error.h"
#include "dos/output.h"
#include "dos/instruction.h"
#include "dos/analysis.h"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stack>
#include <regex>

using namespace std;

void usage() {
    output("usage: mzdiff <base.exe[:entrypoint]> <base.map> <compare.exe[:entrypoint]> [options]\n"
           "options:\n"
           "--verbose: show more detailed information, including compared instructions"
           "--debug:   show additional debug information\n"
           "--nocpu:   omit CPU-related information like instruction decoding\n"
           "--noanal:  omit analysis-related information", LOG_OTHER, LOG_ERROR);
    exit(1);
}

void fatal(const string &msg) {
    output("ERROR: "s + msg, LOG_OTHER, LOG_ERROR);
    exit(1);
}

void info(const string &msg) {
    output(msg, LOG_OTHER, LOG_ERROR);
}

void debug(const string &msg) {
    output(msg, LOG_OTHER, LOG_DEBUG);
}

string mzInfo(const MzImage &mz) {
    ostringstream str;
    str << mz.path() << ", load module of size " <<  mz.loadModuleSize() << " at " << hexVal(mz.loadModuleOffset()) 
        << ", entrypoint at " << mz.entrypoint() << ", stack at " << mz.stackPointer();
    return str.str();
}

Executable loadExe(const string &spec) {
    static const regex EXESPEC_RE{"([-_./a-zA-Z0-9]*)(:([a-fA-F0-9]+))?"};
    smatch match;
    if (!regex_match(spec, match, EXESPEC_RE)) {
        fatal("Invalid exe spec string: "s + spec);
    }
    string path = match[1].str(), entrypoint;
    if (match.size() > 3) entrypoint = match[3].str();
    const auto stat = checkFile(path);
    if (!stat.exists) fatal("File does not exist: "s + path);
    else if (stat.size <= MZ_HEADER_SIZE) fatal("File too small ("s + to_string(stat.size) + "B): " + path); 

    MzImage mz{path};
    mz.load(0);
    debug(mzInfo(mz));
    if (entrypoint.empty())
        return Executable(mz);
    else {
        Offset epOffset = static_cast<Offset>(stoi(entrypoint, nullptr, 16));
        Address epAddr(epOffset);
        debug("Entrypoint override: "s + epAddr.toString());
        return Executable(mz, epAddr);
    }
}

int main(int argc, char *argv[]) {
    setOutputLevel(LOG_WARN);
    if (argc < 4) {
        usage();
    }
    for (int aidx = 4; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--verbose") setOutputLevel(LOG_VERBOSE);
        else if (arg == "--nocpu") setModuleVisibility(LOG_CPU, false);
        else if (arg == "--noanal") setModuleVisibility(LOG_ANALYSIS, false);
        else fatal("Unrecognized parameter: "s + arg);
    }
    const string baseSpec{argv[1]}, pathMap{argv[2]}, compareSpec{argv[3]};
    try {
        Executable exeBase = loadExe(baseSpec);
        Executable exeCompare = loadExe(compareSpec);
        RoutineMap map(pathMap);
        if (!exeBase.compareCode(map, exeCompare))
            return 1;
    }
    catch (Error &e) {
        fatal(e.why());
    }
    catch (...) {
        fatal("Unknown exception");
    }
    return 0;
}