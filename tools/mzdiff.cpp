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

// TODO: 
// - mode to figure out main() address automatically, call to main seems to be always at offset 0x8d into MSC start function code

void usage() {
    output("usage: mzdiff [options] base.exe[:entrypoint] compare.exe[:entrypoint]\n"
           "Compares two DOS MZ executables instruction by instruction, accounting for differences in code layout\n"
           "Options:\n"
           "--map basemap  map file of base executable to use, if present\n"
           "--verbose      show more detailed information, including compared instructions\n"
           "--debug        show additional debug information\n"
           "--dbgcpu       include CPU-related debug information like instruction decoding\n"
           "--idiff        ignore differences completely\n"
           "--nocall       do not follow calls, useful for comparing single functions\n"
           "--sdiff count  skip differences, ignore up to 'count' consecutive mismatching instructions in the base executable\n"
           "--loose        non-strict matching, allows e.g for literal argument differences",
           LOG_OTHER, LOG_ERROR);
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

Executable loadExe(const string &spec, const Word segment, AnalysisOptions &opt) {
    // TODO: support providing segment for entrypoint
    static const regex EXESPEC_RE{"([-_./a-zA-Z0-9]*)(:([xa-fA-F0-9]+)(-([xa-fA-F0-9]+))?)?"};
    smatch match;
    if (!regex_match(spec, match, EXESPEC_RE)) {
        fatal("Invalid exe spec string: "s + spec);
    }
    string path = match[1].str();
    const auto stat = checkFile(path);
    if (!stat.exists) fatal("File does not exist: "s + path);
    else if (stat.size <= MZ_HEADER_SIZE) fatal("File too small ("s + to_string(stat.size) + "B): " + path); 

    MzImage mz{path};
    mz.load(segment);
    debug(mzInfo(mz));
    Executable exe{mz};

    // handle entrypoint override from command line
    if (!match[3].str().empty()) {
        Address epAddr{match[3].str(), false}; // do not normalize address
        debug("Entrypoint override: "s + epAddr.toString());
        exe.setEntrypoint(epAddr);
    }
    // handle stop address on command line
    if (!match[5].str().empty() && !opt.stopAddr.isValid()) {
        Address stopAddr{match[5].str(), false};
        stopAddr.relocate(segment);
        if (stopAddr <= exe.entrypoint()) 
            fatal("Stop address " + stopAddr.toString() + " before executable entrypoint " + exe.entrypoint().toString());
        debug("Stop address: "s + stopAddr.toString());
        opt.stopAddr = stopAddr;
    }

    return exe;
}

int main(int argc, char *argv[]) {
    const Word loadSeg = 0x1000;
    setOutputLevel(LOG_WARN);
    setModuleVisibility(LOG_CPU, false);
    if (argc < 3) {
        usage();
    }
    AnalysisOptions opt;
    string baseSpec, pathMap, compareSpec;
    int posarg = 0;
    for (int aidx = 1; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--verbose") setOutputLevel(LOG_VERBOSE);
        else if (arg == "--dbgcpu") setModuleVisibility(LOG_CPU, true);
        else if (arg == "--idiff") opt.ignoreDiff = true;
        else if (arg == "--nocall") opt.noCall = true;
        else if (arg == "--sdiff") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --sdiff");
            opt.skipDiff = stoi(argv[++aidx], nullptr, 10);
        }
        else if (arg == "--map") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --map");
            pathMap = argv[++aidx];
        }        
        else if (arg == "--loose") opt.strict = false;
        else { // positional arguments
            switch (++posarg) {
            case 1: baseSpec = arg; break;
            case 2: compareSpec = arg; break;
            default: fatal("Unrecognized argument: " + arg);
            }
        }
    }
    try {
        Executable exeBase = loadExe(baseSpec, loadSeg, opt);
        Executable exeCompare = loadExe(compareSpec, loadSeg, opt);
        RoutineMap map;
        if (!pathMap.empty()) map = {pathMap, loadSeg};
        if (!exeBase.compareCode(map, exeCompare, opt)) return 1;
    }
    catch (Error &e) {
        fatal(e.why());
    }
    catch (...) {
        fatal("Unknown exception");
    }
    return 0;
}