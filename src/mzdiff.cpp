#include "dos/util.h"
#include "dos/mz.h"
#include "dos/error.h"
#include "dos/output.h"
#include "dos/executable.h"
#include "dos/analysis.h"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stack>
#include <regex>

using namespace std;

OUTPUT_CONF(LOG_SYSTEM)

void usage() {
    output("mzdiff v" + VERSION + "\n"
           "usage: mzdiff [options] reference.exe[:entrypoint] target.exe[:entrypoint]\n"
           "Compares two DOS MZ executables instruction by instruction, accounting for differences in code layout\n"
           "Options:\n"
           "--map path[:tag]  map file of reference executable (recommended, otherwise functionality limited)\n"
           "--tmap path[:tag] map file of target executable (optional)\n"
           "--verbose         show more detailed information, including compared instructions\n"
           "--debug           show additional debug information\n"
           "--dbgcpu          include CPU-related debug information like instruction decoding\n"
           "--idiff           ignore differences completely\n"
           "--nocall          do not follow calls, useful for comparing single functions\n"
           "--nosym           do not display symbol names in the disassembly\n"
           "--asm             descend into routines marked as assembly in the map, normally skipped\n"
           "--nostat          do not display comparison statistics at the end\n"
           "--rskip count     ignore up to 'count' consecutive mismatching instructions in the reference executable\n"
           "--tskip count     ignore up to 'count' consecutive mismatching instructions in the target executable\n"
           "--ctx count       display up to 'count' context instructions after a mismatch (default 10)\n"
           "--dctx count      display up to 'count' bytes around a data segment mismatch (default 160)\n"
           "--loose           non-strict matching, allows e.g for literal argument differences\n"
           "--variant         treat instruction variants that do the same thing as matching\n"
           "--data segname    compare data segment contents instead of code\n"
           "--tdata segname   target data segment name to compare if different than reference\n"
           "--extdata         include variables marked as external in data comparison\n"
           "1) The optional entrypoint spec tells the tool at which offset to start comparing, and can be different\n"
           "   for both executables if their layout does not match. It can be any of the following:\n"
           "    ':0x12345' for a hex offset\n"
           "    ':0x123-0x567' for a hex range\n"
           "    ':[ab12??ea]' for a hexa string to search for and use its offset. The string must consist of an even amount\n"
           "     of hexa characters, and '\?\?' will match any byte value.\n"
           "    'routine_name' for a routine name present in the corresponding map (reference or target)"
           "  In case of a range being specified as the spec, the search will stop with a success on reaching the latter offset.\n"
           "  If the spec is not present, the tool will use the CS:IP address from the MZ header as the entrypoint.\n"
           "2) The target map file is used as a reference for variable location in data segment comparison mode,\n"
           "   or as an optional reference to lookup target executable locations for comparison if they could not be determined\n"
           "   from watching encountered calls. In the latter case, the path can be followed by an optional tag which indicates\n"
           "   the format of the map file:\n"
           "     'ida' for an IDA listing file\n"
           "     'link' for an MS LINK mapfile\n"
           "3) The tool also saves a map file of the target executable that's been discovered in the course of the comparison\n"
           "   to the path of the reference map with the extension changed to '.tgt'",
           LOG_OTHER, LOG_ERROR);
    exit(1);
}

void fatal(const string &msg) {
    error(msg);
    exit(1);
}

string mzInfo(const MzImage &mz) {
    ostringstream str;
    str << mz.path() << ", load module of size " <<  mz.loadModuleSize() << " at " << hexVal(mz.loadModuleOffset()) 
        << ", entrypoint at " << mz.entrypoint() << ", stack at " << mz.stackPointer();
    return str.str();
}

Executable loadExe(const string &spec, const string &mapSpec, const Word segment, Analyzer::Options &opt, const bool base) {
    debug("Loading executable: " + spec);
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

    if (path.empty()) {
        if (base) fatal("Empty reference executable path");
        else fatal("Empty target executable path");
    }
    const auto stat = checkFile(path);
    if (!stat.exists) fatal("Executable file does not exist: "s + path);
    else if (stat.size <= MZ_HEADER_SIZE) fatal("Executable file too small ("s + to_string(stat.size) + "B): " + path);
    MzImage mz{path};
    mz.load(segment);
    debug(mzInfo(mz));
    Executable exe{mz};

    // parse code map if provided
    CodeMap &codeMap = exe.map();
    if (!mapSpec.empty()) {
        static const regex TMAP_RE{R"(([^:]+)(?::([a-zA-Z]+))?)"};
        std::smatch match;
        if (!regex_match(mapSpec, match, TMAP_RE)) fatal("Invalid executable map path: " + mapSpec);
        const string path = match[1].str(), tag = match[2].str();
        verbose("Loading executable map from " + path + ", tag: " + tag);
        CodeMap::Type type = CodeMap::MAP_MZRE;
        if (tag == "ida") type = CodeMap::MAP_IDALST;
        else if (tag == "link") type = CodeMap::MAP_MSLINK;
        codeMap = { path, segment, type };
    }

    // entrypoint override from envvar
    const char *routineEnv = getenv("MZDIFF_ROUTINE");
    if (routineEnv) entry = string(routineEnv);
    // use default entrypoint, done
    if (entry.empty()) return exe;

    // otherwise handle entrypoint override from command line
    static const regex 
        OFFSET_RE{"([xa-fA-F0-9]+)(-([xa-fA-F0-9]+))?"},
        HEXASTR_RE{"\\[([?a-fA-F0-9]+)\\]"},
        ROUTINENAME_RE{"([_a-zA-Z0-9]+)"};
    smatch match;
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
    } else if (regex_match(entry, match, HEXASTR_RE)) {
        // use location of provided hexa string as entrypoint, if found in the executable
        const string hexa = match[1].str();
        debug("Entrypoint search for location of '" + hexa + "'");
        auto pattern = hexaToNumeric(hexa);
        Address ep = exe.find(pattern);
        if (!ep.isValid()) fatal("Could not find pattern '" + hexa + "' in " + path);
        debug("Pattern found at " + ep.toString());
        // do not relocate, search already performed on relocated addresses
        exe.setEntrypoint(ep, false);
    } else if (regex_match(entry, match, ROUTINENAME_RE)) {
        if (codeMap.empty()) fatal("Need valid code map for specifying entrypoint by name");
        const string name = match[1].str();
        const Routine r = codeMap.getRoutine(name);
        if (!r.isValid()) fatal("Unable to find routine '" + name + "' in code map for " + path);
        debug("Entrypoint from routine name " + name + ": " + r.entrypoint().toString());
        exe.setEntrypoint(r.entrypoint(), false);
    }
    else fatal("Invalid exe spec string: "s + entry);

    return exe;
}

int main(int argc, char *argv[]) {
    const Word loadSeg = 0x0;
    setOutputLevel(LOG_WARN);
    setModuleVisibility(LOG_CPU, false);
    if (argc < 3) {
        usage();
    }
    // parse cmdline args
    Analyzer::Options opt;
    string refSpec, refMapSpec, tgtSpec, dataSegment, tdataSegment, tgtMapSpec;
    int posarg = 0;
    for (int aidx = 1; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--verbose") setOutputLevel(LOG_VERBOSE);
        else if (arg == "--dbgcpu") setModuleVisibility(LOG_CPU, true);
        else if (arg == "--idiff") opt.ignoreDiff = true;
        else if (arg == "--nocall") opt.noCall = true;
        else if (arg == "--nosym") opt.noSym = true;
        else if (arg == "--asm") opt.checkAsm = true;
        else if (arg == "--nostat") opt.noStats = true;
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
        else if (arg == "--dctx") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --dctx");
            opt.dataCtxCount = stoi(argv[++aidx], nullptr, 10);
        }        
        else if (arg == "--map") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --map");
            refMapSpec = argv[++aidx];
            opt.mapPath = refMapSpec;
        }
        else if (arg == "--tmap") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --tmap");
            tgtMapSpec = argv[++aidx];
        }
        else if (arg == "--loose") opt.strict = false;
        else if (arg == "--variant") opt.variant = true;
        else if (arg == "--data") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --data");
            dataSegment = argv[++aidx];
        }
        else if (arg == "--tdata") {
            if (aidx + 1 >= argc) fatal("Option requires an argument: --tdata");
            tdataSegment = argv[++aidx];
        }
        else if (arg == "--extdata") opt.extData = true;
        else if (arg.starts_with("--")) fatal("Unrecognized option: " + arg);
        else { // positional arguments
            switch (++posarg) {
            case 1: refSpec = arg; break;
            case 2: tgtSpec = arg; break;
            default: fatal("Unrecognized argument: " + arg);
            }
        }
    }

    // actually do stuff
    bool compareResult = false;
    try {
        Executable 
            exeRef = loadExe(refSpec, refMapSpec, loadSeg, opt, true),
            exeTgt = loadExe(tgtSpec, tgtMapSpec, loadSeg, opt, false);
        Analyzer a{opt};
        // code comparison
        if (dataSegment.empty()) {
            compareResult = a.compareCode(exeRef, exeTgt);
        }
        // data comparison
        else {
            if (refMapSpec.empty()) fatal("Data comparison needs a map of the reference executable, use --map");
            if (tgtMapSpec.empty()) {
                tgtMapSpec = replaceExtension(refMapSpec, "tgt");
                if (!checkFile(tgtMapSpec).exists) fatal("No target map provided with --tmap for data comparison and guessed location " + tgtMapSpec + " does not exist");
                verbose("Using guessed target map location: " + tgtMapSpec);
            }
            compareResult = a.compareData(exeRef, exeTgt, dataSegment, tdataSegment);
        }
    }
    catch (Error &e) {
        fatal(e.why());
    }
    catch (std::exception &e) {
        fatal(string(e.what()));
    }    
    catch (...) {
        fatal("Unknown exception");
    }
    return compareResult ? 0 : 1;
}