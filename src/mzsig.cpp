#include "dos/output.h"
#include "dos/error.h"
#include "dos/executable.h"
#include "dos/codemap.h"
#include "dos/signature.h"
#include "dos/util.h"

// TODO:
// - extract signatures from OMF object files/libraries
// - save output in IDA PAT format

using namespace std;

OUTPUT_CONF(LOG_SYSTEM)

void usage() {
    ostringstream str;
    str << "mzsig v" << VERSION << endl
        << "Usage: mzsig [options] exe_file map_file output_file" << endl
        << "Extracts routines from exe_file at locations specified by map_file and saves their signatures to output_file" << endl
        << "This is useful for finding routine duplicates from exe_file in other executables using mzdup" << endl
        << "Options:" << endl
        << "--verbose:       show information about extracted routines" << endl
        << "--debug:         show additional debug information" << endl
        << "--overwrite:     overwrite output file if exists";
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
    string exePath, mapPath, outPath;
    bool overwrite = false;
    Word loadSegment = 0;
    for (int aidx = 1; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--verbose") setOutputLevel(LOG_VERBOSE);
        else if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--overwrite") overwrite = true;
        else if (exePath.empty()) exePath = arg;
        else if (mapPath.empty()) mapPath = arg;
        else if (outPath.empty()) outPath = arg;
        else fatal("Unrecognized argument: "s + arg);
    }
    try {
        if (!checkFile(exePath).exists) fatal("Executable file does not exist: " + exePath);
        if (!checkFile(mapPath).exists) fatal("Map file does not exist: " + mapPath);
        if (!overwrite && checkFile(outPath).exists) fatal("Output file already exists: " + outPath);
        CodeMap map{mapPath, loadSegment, CodeMap::MAP_MZRE};
        info("Loaded map file " + mapPath + ": " + to_string(map.segmentCount()) + " segments, " + to_string(map.routineCount()) + " routines, " + to_string(map.variableCount()) + " variables");
        MzImage mz{exePath, loadSegment};
        Executable exe{mz};
        SignatureLibrary sig{map, exe};
        info("Extracted signatures from " + to_string(sig.signatureCount()) + " routines, saving to " + outPath);
        sig.save(outPath);
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