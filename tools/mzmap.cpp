#include "dos/util.h"
#include "dos/mz.h"
#include "dos/error.h"
#include "dos/output.h"
#include "dos/executable.h"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stack>
#include <regex>

using namespace std;

void usage() {
    output("usage: mzmap <file.exe[:entrypoint]> <output.map> [options]\n"
           "Scans a DOS MZ executable and tries to find routine boundaries, saves output into a file\n"
           "Options:\n"
           "--verbose:      show more detailed information, including compared instructions\n"
           "--debug:        show additional debug information\n"
           "--nocpu:        omit CPU-related information like instruction decoding\n"
           "--noanal:       omit analysis-related information\n"
           "--load segment: overrride default load segment (0x1000)", LOG_OTHER, LOG_ERROR);
    exit(1);
}

void fatal(const string &msg) {
    output("ERROR: "s + msg, LOG_OTHER, LOG_ERROR);
    exit(1);
}

void info(const string &msg) {
    output(msg, LOG_OTHER, LOG_ERROR);
}

void verbose(const string &msg, const bool noNewline = false) {
    output(msg, LOG_OTHER, LOG_VERBOSE, noNewline);
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

Executable loadExe(const string &spec, const Word loadSegment) {
    static const regex EXESPEC_RE{"([-_./a-zA-Z0-9]*)(:([xa-fA-F0-9]+))?"};
    smatch match;
    if (!regex_match(spec, match, EXESPEC_RE)) {
        fatal("Invalid exe spec string: "s + spec);
    }
    string path = match[1].str(), entrypoint;
    const auto stat = checkFile(path);
    if (!stat.exists) fatal("File does not exist: "s + path);
    else if (stat.size <= MZ_HEADER_SIZE) fatal("File too small ("s + to_string(stat.size) + "B): " + path); 
    verbose("Loading executable "s + path + " at segment "s + hexVal(loadSegment));
    MzImage mz{path};
    mz.load(loadSegment);
    debug(mzInfo(mz));
    Executable exe{mz};

    const string epStr = match[3].str();
    if (!epStr.empty()) {
        Address epAddr(epStr, false);
        debug("Entrypoint override: "s + epAddr.toString());
        exe.setEntrypoint(epAddr);
    }
    return exe;
}

int main(int argc, char *argv[]) {
    setOutputLevel(LOG_WARN);
    if (argc < 3) {
        usage();
    }
    Word loadSegment = 0x1000;
    for (int aidx = 3; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--verbose") setOutputLevel(LOG_VERBOSE);
        else if (arg == "--nocpu") setModuleVisibility(LOG_CPU, false);
        else if (arg == "--noanal") setModuleVisibility(LOG_ANALYSIS, false);
        else if (arg == "--load" && (aidx + 1 < argc)) {
            string loadSegStr(argv[aidx+1]);
            loadSegment = static_cast<Word>(stoi(loadSegStr, nullptr, 16));
            verbose("Overloading default load segment: "s + hexVal(loadSegment));
        }
        else fatal("Unrecognized parameter: "s + arg);
    }
    const string spec{argv[1]}, pathMap{argv[2]};
    try {
        Executable exe = loadExe(spec, loadSegment);
        RoutineMap map = exe.findRoutines();
        if (map.empty()) {
            fatal("Unable to find any routines");
            return 1;
        }
        verbose(map.dump(), true);
        map.save(pathMap, loadSegment);
    }
    catch (Error &e) {
        fatal(e.why());
    }
    catch (...) {
        fatal("Unknown exception");
    }
    return 0;
}