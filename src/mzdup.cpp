#include "dos/output.h"
#include "dos/analysis.h"
#include "dos/error.h"
#include "dos/executable.h"

using namespace std;

OUTPUT_CONF(LOG_SYSTEM)

Analyzer::Options options;

void usage() {
    ostringstream str;
    str << "mzdup v" << VERSION << endl
        << "Usage: mzdup [options] reference_exe reference_map target_exe target_map" << endl
        << "Attempts to identify potential duplicate routines between two DOS MZ executables" << endl
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
    string refExePath, refMapPath, tgtExePath, tgtMapPath;
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
    catch (std::exception &e) {
        fatal(string(e.what()));
    }
    catch (...) {
        fatal("Unknown exception");
    }
    return 0;
}