#include "dos/output.h"
#include "dos/analysis.h"
#include "dos/error.h"
#include "dos/executable.h"
#include "dos/util.h"

using namespace std;

OUTPUT_CONF(LOG_SYSTEM)

Analyzer::Options options;

void usage() {
    ostringstream str;
    str << "mzdup v" << VERSION << endl
        << "Usage: mzdup [options] signature_file target_exe target_map" << endl
        << "Attempts to identify duplicate routines matching the signatures from the input file in the target executable" << endl
        << "Updated target_map with duplicates marked will be saved to target_map.dup" << endl
        << "Options:" << endl
        << "--verbose:       show more detailed information about processed routines" << endl
        << "--debug:         show additional debug information" << endl
        << "--minsize count: don't search for duplicates of routines smaller than 'count' instructions (default: " << options.routineSizeThresh << ")" << endl
        << "--maxdist count: routines differing by no more than 'count' instructions will be reported as duplicates (default: " << options.routineDistanceThresh << ")";
    output(str.str(), LOG_OTHER, LOG_ERROR);
    exit(1);
}

void fatal(const string &msg) {
    error(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    setOutputLevel(LOG_INFO);
    setModuleVisibility(LOG_CPU, false);
    if (argc < 4) {
        usage();
    }
    Word loadSegment = 0x1000;
    string sigFilePath, tgtExePath, tgtMapPath;
    Size minSize = options.routineSizeThresh, maxDist = options.routineDistanceThresh;
    for (int aidx = 1; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--verbose") { setOutputLevel(LOG_VERBOSE); }
        else if (arg == "--minsize" && (aidx++ + 1 < argc)) {
            string minSizeStr(argv[aidx]);
            minSize = stoi(minSizeStr, nullptr, 10);
        }
        else if (arg == "--maxdist" && (aidx++ + 1 < argc)) {
            string maxDistStr(argv[aidx]);
            maxDist = stoi(maxDistStr, nullptr, 10);
        }
        else if (sigFilePath.empty()) sigFilePath = arg;
        else if (tgtExePath.empty()) tgtExePath = arg;
        else if (tgtMapPath.empty()) tgtMapPath = arg;
        else fatal("Unrecognized argument: "s + arg);
    }
    if (sigFilePath.empty()) fatal("Signature file path was not provided");
    if (tgtExePath.empty()) fatal("Target executable file path was not provided");
    if (tgtMapPath.empty()) fatal("Target executable map file path was not provided");
    if (!checkFile(sigFilePath).exists) fatal("Signature file " + sigFilePath + " does not exist");
    if (!checkFile(tgtExePath).exists) fatal("Target executable file " + tgtExePath + " does not exist");
    if (!checkFile(tgtMapPath).exists) fatal("Target executable map " + tgtMapPath + " does not exist");    
    try {
        options.routineSizeThresh = minSize;
        options.routineDistanceThresh = maxDist;
        SignatureLibrary sigs{sigFilePath};
        MzImage tgtMz{tgtExePath, loadSegment};
        Executable tgt{tgtMz};
        CodeMap tgtMap{tgtMapPath, loadSegment};
        if (tgtMap.codeSize() != tgtMz.loadModuleSize()) throw LogicError("Reference map size " + sizeStr(tgtMap.codeSize()) + " does not match size of executable load module: " + sizeStr(tgtMz.loadModuleSize()));
        Analyzer a{options};
        // search for duplicates, write updated target map if any found
        if (a.findDuplicates(sigs, tgt, tgtMap)) tgtMap.save(tgtMapPath + ".dup", loadSegment, true);
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