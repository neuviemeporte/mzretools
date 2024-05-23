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
    output("usage: mzdiff [options] base.exe[:entrypoint] compare.exe[:entrypoint]\n"
           "Compares two DOS MZ executables instruction by instruction, accounting for differences in code layout\n"
           "Options:\n"
           "--map basemap  map file of base executable to use, if present\n"
           "--exclude pat  regex pattern of function names to exclude in comparison\n"
           "--verbose      show more detailed information, including compared instructions\n"
           "--debug        show additional debug information\n"
           "--dbgcpu       include CPU-related debug information like instruction decoding\n"
           "--idiff        ignore differences completely\n"
           "--nocall       do not follow calls, useful for comparing single functions\n"
           "--rskip count  skip differences, ignore up to 'count' consecutive mismatching instructions in the reference executable\n"
           "--tskip count  skip differences, ignore up to 'count' consecutive mismatching instructions in the target executable\n"
           "--ctx count    display up to 'count' context instructions after a mismatch (default 10)\n"
           "--loose        non-strict matching, allows e.g for literal argument differences\n"
           "--variant      treat instruction variants that do the same thing as matching\n"
           "The optional entrypoint spec tells the tool at which offset to start comparing, and can be different\n"
           "for both executables if their layout does not match. It can be any of the following:\n"
           "  ':0x123' for a hex offset\n"
           "  ':0x123-0x567' for a hex range\n"
           "  ':[ab12??ea]' for a hexa string to search for and use its offset. The string must consist of an even amount\n"
           "   of hexa characters, and '\?\?' will match any byte value.\n"
           "In case of a range being specified as the spec, the search will stop with a success on reaching the latter offset.\n"
           "If the spec is not present, the tool will use the CS:IP address from the MZ header as the entrypoint.",
           LOG_OTHER, LOG_ERROR);
    exit(1);
}

void fatal(const string &msg) {
    output("ERROR: "s + msg, LOG_OTHER, LOG_ERROR);
    exit(1);
}

void info(const string &msg) {
    output(msg, LOG_OTHER, LOG_INFO);
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
    string path, entry;
    // split spec string into the path and the entrypoint specification, if present
    auto specPos = spec.find(":");
    if (specPos == string::npos) {
        path = spec;
    } 
    else {
        path = spec.substr(0, specPos);
        entry = spec.substr(specPos + 1);
    }

    const auto stat = checkFile(path);
    if (!stat.exists) fatal("File does not exist: "s + path);
    else if (stat.size <= MZ_HEADER_SIZE) fatal("File too small ("s + to_string(stat.size) + "B): " + path); 
    MzImage mz{path};
    mz.load(segment);
    debug(mzInfo(mz));
    Executable exe{mz};

    // use default entrypoint, done
    if (entry.empty()) return exe;

    static const regex 
        OFFSET_RE{"([xa-fA-F0-9]+)(-([xa-fA-F0-9]+))?"},
        HEXASTR_RE{"\\[([?a-fA-F0-9]+)\\]"};

    smatch match;
    // otherwise handle entrypoint override from command line
    if (regex_match(entry, match, OFFSET_RE)) {
        if (!match[1].str().empty()) {
            Address epAddr{match[1].str(), false}; // do not normalize address
            debug("Entrypoint override: "s + epAddr.toString());
            exe.setEntrypoint(epAddr);
        }
        // handle stop address on command line
        if (!match[3].str().empty() && !opt.stopAddr.isValid()) {
            Address stopAddr{match[3].str(), false};
            stopAddr.relocate(segment);
            if (stopAddr <= exe.entrypoint()) 
                fatal("Stop address " + stopAddr.toString() + " before executable entrypoint " + exe.entrypoint().toString());
            debug("Stop address: "s + stopAddr.toString());
            opt.stopAddr = stopAddr;
        }
    }
    // use location of provided hexa string as entrypoint, if found in the executable
    else if (regex_match(entry, match, HEXASTR_RE)) {
        const string hexa = match[1].str();
        debug("Entrypoint search at location of '" + hexa + "'");
        auto pattern = hexaToNumeric(hexa);
        Address ep = mz.find(pattern);
        if (!ep.isValid()) fatal("Could not find pattern '" + hexa + "' in " + path);
        debug("pattern found at " + ep.toString());
        exe.setEntrypoint(ep);
    }
    else fatal("Invalid exe spec string: "s + entry);
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
        else if (arg == "--rskip") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --rskip");
            opt.refSkip = stoi(argv[++aidx], nullptr, 10);
        }
        else if (arg == "--tskip") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --tskip");
            opt.tgtSkip = stoi(argv[++aidx], nullptr, 10);
        }
        else if (arg == "--ctx") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --ctx");
            opt.ctxCount = stoi(argv[++aidx], nullptr, 10);
        }
        else if (arg == "--map") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --map");
            pathMap = argv[++aidx];
        }
        else if (arg == "--exclude") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --exclude");
            opt.exclude = argv[++aidx];
        }        
        else if (arg == "--loose") opt.strict = false;
        else if (arg == "--variant") opt.variant = true;
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
        Size mapSize = 0;
        if (!pathMap.empty()) {
            map = {pathMap, loadSeg};
            mapSize = map.size();
        } 
        bool compareResult = exeBase.compareCode(map, exeCompare, opt);
        return compareResult ? 0 : 1;
    }
    catch (Error &e) {
        fatal(e.why());
    }
    catch (...) {
        fatal("Unknown exception");
    }
    return 0;
}