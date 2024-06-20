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
    output("usage: mzmap [options] [<file.exe[:entrypoint]>] <output.map>\n"
           "Scans a DOS MZ executable and tries to find routine boundaries, saves output into an editable map file\n"
           "Without an exe file, prints a summary of an existing map file"
           "Options:\n"
           "--verbose:      show more detailed information, including compared instructions\n"
           "--debug:        show additional debug information\n"
           "--hide:         only show uncompleted and unclaimed areas in map summary\n"
           "--format:       format printed routines in a way that's directly writable back to the map file\n"
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

void loadAndPrintMap(const string &mapfile, const bool verbose, const bool hide, const bool format) {
    debug("Single parameter specified, printing existing mapfile");
    auto fs = checkFile(mapfile);
    if (!fs.exists) fatal("Mapfile does not exist: " + mapfile);
    RoutineMap map(mapfile);
    cout << map.dump(verbose, hide, format);
}

int main(int argc, char *argv[]) {
    setOutputLevel(LOG_INFO);
    if (argc < 2) {
        usage();
    }
    Word loadSegment = 0x1000;
    string file1, file2;
    bool verbose = false;
    bool hide = false, format = false;
    for (int aidx = 1; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--verbose") { verbose = true; setOutputLevel(LOG_VERBOSE); }
        else if (arg == "--nocpu") setModuleVisibility(LOG_CPU, false);
        else if (arg == "--noanal") setModuleVisibility(LOG_ANALYSIS, false);
        else if (arg == "--hide") hide = true;
        else if (arg == "--format") format = true;
        else if (arg == "--load" && (aidx + 1 < argc)) {
            string loadSegStr(argv[aidx+1]);
            loadSegment = static_cast<Word>(stoi(loadSegStr, nullptr, 16));
            info("Overloading default load segment: "s + hexVal(loadSegment));
        }
        else if (file1.empty()) file1 = arg;
        else if (file2.empty()) file2 = arg;
        else fatal("Unrecognized argument: "s + arg);
    }
    try {
        if (file2.empty()) { // print existing map and exit
            loadAndPrintMap(file1, verbose, hide, format);
        }
        else { // regular operation, scan executable for routines
            Executable exe = loadExe(file1, loadSegment);
            RoutineMap map = exe.findRoutines();
            if (map.empty()) {
                fatal("Unable to find any routines");
                return 1;
            }
            if (verbose) cout << map.dump(verbose, hide);
            map.save(file2, loadSegment);
        }
    }
    catch (Error &e) {
        fatal(e.why());
    }
    catch (...) {
        fatal("Unknown exception");
    }
    return 0;
}