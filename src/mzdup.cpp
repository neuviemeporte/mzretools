#include "dos/output.h"
#include "dos/analysis.h"
#include "dos/error.h"
#include "dos/executable.h"

using namespace std;

OUTPUT_CONF(LOG_SYSTEM)

void usage() {
    output("mzdup v" + VERSION + "\n"
           "usage: mzdup [options] reference_exe reference_map target_exe target_map\n"
           "Attempts to identify potential duplicate routines between two DOS MZ executables\n"
           "Updated target_map with duplicates marked will be saved to target_map.dup\n"
           "Options:\n"
           "--verbose:       show more detailed information about processed routines\n"
           "--debug:         show additional debug information\n"
           "--minsize count: don't search for duplicates of routines smaller than 'count' instructions (default: 10)\n"
           "--maxdist count: routines differing by no more than 'count' instructions will be reported as duplicates (default: 5)",
            LOG_OTHER, LOG_ERROR);
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
    string refExePath, refMapPath, tgtExePath, tgtMapPath;
    Analyzer::Options options;
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
        else if (refExePath.empty()) refExePath = arg;
        else if (refMapPath.empty()) refMapPath = arg;
        else if (tgtExePath.empty()) tgtExePath = arg;
        else if (tgtMapPath.empty()) tgtMapPath = arg;
        else fatal("Unrecognized argument: "s + arg);
    }
    try {
        options.routineSizeThresh = minSize;
        options.routineDistanceThresh = maxDist;
        MzImage refMz{refExePath};
        refMz.load(loadSegment);
        MzImage tgtMz{tgtExePath};
        tgtMz.load(loadSegment);
        Executable ref{refMz}, tgt{tgtMz};
        RoutineMap refMap{refMapPath, loadSegment}, tgtMap{tgtMapPath, loadSegment};
        Analyzer a{options};
        // search for duplicates, write updated target map if any found
        if (a.findDuplicates(ref, tgt, refMap, tgtMap)) tgtMap.save(tgtMapPath + ".dup", loadSegment, true);
    }
    catch (Error &e) {
        fatal(e.why());
    }
    catch (...) {
        fatal("Unknown exception");
    }
    return 0;
}