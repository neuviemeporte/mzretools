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
    output("mzmap v" + VERSION + "\n"
           "usage: mzmap [options] [file.exe[:entrypoint]] file.map\n"
           "Scans a DOS MZ executable trying to find routines and variables, saves output into an editable map file\n"
           "Without an exe file, prints a summary of an existing map file\n"
           "There is limited support for using an IDA .lst file as a map file,\n"
           "and printing a .lst file will also convert and save it as a .map file\n"
           "Options:\n"
           "--verbose:      show more detailed information, including compared instructions\n"
           "--debug:        show additional debug information\n"
           "--overwrite:    overwrite output file.map if already exists\n"
           "--brief:        only show uncompleted and unclaimed areas in map summary\n"
           "--format:       format printed routines in a way that's directly writable back to the map file\n"
           "--nocpu:        omit CPU-related information like instruction decoding from debug output\n"
           "--noanal:       omit analysis-related information from debug output\n"
           "--linkmap file  use a linker map from Microsoft C to seed initial location of routines\n"
           "--load segment: override default load segment (0x0)", LOG_OTHER, LOG_ERROR);
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
    info("Loading executable "s + path + " at segment "s + hexVal(loadSegment));
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

void loadAndPrintMap(const string &mapfile, const bool verbose, const bool brief, const bool format) {
    info("Single parameter specified, printing existing mapfile");
    auto fs = checkFile(mapfile);
    if (!fs.exists) fatal("Mapfile does not exist: " + mapfile);
    string mapfileLower(mapfile.size(), '\0');
    std::transform(mapfile.begin(), mapfile.end(), mapfileLower.begin(), [](unsigned char c){ return std::tolower(c); });
    const string ext = getExtension(mapfileLower);
    CodeMap::Type mapType = CodeMap::MAP_MZRE;
    if (ext == "lst") mapType = CodeMap::MAP_IDALST;
    // TODO: support printing link maps?
    CodeMap map(mapfile, 0, mapType);
    const auto sum = map.getSummary(verbose, brief, format);
    cout << sum.text;
    if (map.isIda()) map.save(mapfile + ".map");
}

int main(int argc, char *argv[]) {
    setOutputLevel(LOG_INFO);
    if (argc < 2) {
        usage();
    }
    Word loadSegment = 0x1000;
    string file1, file2, linkmapPath;
    bool verbose = false;
    bool brief = false, format = false, overwrite = false;
    for (int aidx = 1; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--verbose") { verbose = true; setOutputLevel(LOG_VERBOSE); }
        else if (arg == "--nocpu") setModuleVisibility(LOG_CPU, false);
        else if (arg == "--noanal") setModuleVisibility(LOG_ANALYSIS, false);
        else if (arg == "--overwrite") overwrite = true;
        else if (arg == "--brief") {
            info("Showing only only uncompleted routines and unclaimed blocks in code segments");
            brief = true;
        }
        else if (arg == "--format") format = true;
        else if (arg == "--load") {
            if (++aidx >= argc) fatal("Option requires an argument: --load");
            string loadSegStr(argv[aidx]);
            loadSegment = static_cast<Word>(stoi(loadSegStr, nullptr, 16));
            info("Overloading default load segment: "s + hexVal(loadSegment));
        }
        else if (arg == "--linkmap") {
            if (++aidx >= argc) fatal("Option requires an argument: --linkmap");
            linkmapPath = string{argv[aidx]};
            if (!checkFile(linkmapPath).exists) fatal("Linker map file does not exist: " + linkmapPath);
        }
        else if (file1.empty()) file1 = arg;
        else if (file2.empty()) file2 = arg;
        else fatal("Unrecognized argument: "s + arg);
    }
    try {
        if (file1.empty()) fatal("Need at least one input file");
        if (file2.empty()) { // print existing map and exit
            loadAndPrintMap(file1, verbose, brief, format);
        }
        else { // regular operation, scan executable for routines
            if (!overwrite && checkFile(file2).exists) {
                fatal("Output file already exists: " + file2);
                return 1;
            }
            Executable exe = loadExe(file1, loadSegment);
            Analyzer a = Analyzer(Analyzer::Options());
            // optionally seed search queue with link map
            if (!linkmapPath.empty()) {
                CodeMap linkmap{linkmapPath, loadSegment, CodeMap::MAP_MSLINK};
                info("Using linker map file " + linkmapPath + " to seed scan: " + to_string(linkmap.segmentCount()) + " segments, " + to_string(linkmap.routineCount()) + " routines, " + to_string(linkmap.variableCount()) + " variables");
                a.seedQueue(linkmap, exe);
            }
            CodeMap map = a.exploreCode(exe);
            if (map.empty()) {
                fatal("Unable to find any routines");
                return 1;
            }
            if (verbose) cout << map.getSummary(verbose, brief).text;
            map.save(file2, loadSegment, overwrite);
            info("Please review the output file (" + file2 + "), assign names to routines/segments\nYou may need to resolve inaccuracies with routine block ranges manually; this tool is not perfect");
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
    return 0;
}