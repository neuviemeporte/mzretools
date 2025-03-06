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

const Size 
    MIN_DEFAULT = 10,
    MAX_DEFAULT = 0;

void usage() {
    ostringstream str;
    str << "mzsig v" << VERSION << endl
        << "Usage: " << endl
        << "mzsig [options] exe_file map_file output_file" << endl
        << "    Extracts routines from exe_file at locations specified by map_file and saves their signatures to output_file" << endl
        << "    This is useful for finding routine duplicates from exe_file in other executables using mzdup" << endl
        << "mzsig [options] signature_file" << endl
        << "    Displays the contents of the signature file" << endl
        << "Options:" << endl
        << "--verbose       show information about extracted routines" << endl
        << "--debug         show additional debug information" << endl
        << "--overwrite     overwrite output file if exists" << endl
        << "--min count     ignore routines smaller than 'count' instructions (default: " << to_string(MIN_DEFAULT) << ")" << endl
        << "--max count     ignore routines larger than 'count' instructions (0: no limit, default: " << to_string(MAX_DEFAULT) << ")" << endl
        << "You can prevent specific routines from having their signatures extracted by annotating them with 'ignore' in the map file," << endl
        << "see the map file format documentation for more information."; 
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
    if (argc < 2) {
        usage();
    }
    string path1, path2, path3;
    bool overwrite = false;
    Word loadSegment = 0x1000;
    Size minInstructions = MIN_DEFAULT, maxInstructions = MAX_DEFAULT;
    for (int aidx = 1; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--verbose") setOutputLevel(LOG_VERBOSE);
        else if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--overwrite") overwrite = true;
        else if (arg == "--min" && ++aidx < argc) {
            string countStr{argv[aidx]};
            minInstructions = stoi(countStr, nullptr, 10);
        }
        else if (arg == "--max" && ++aidx < argc) {
            string countStr{argv[aidx]};
            maxInstructions = stoi(countStr, nullptr, 10);
        }
        else if (path1.empty()) path1 = arg;
        else if (path2.empty()) path2 = arg;
        else if (path3.empty()) path3 = arg;
        else fatal("Unrecognized argument: "s + arg);
    }
    try {
        if (path2.empty()) {
            SignatureLibrary sig{path1};
            info("Loaded signatures from " + path1 + ", " + to_string(sig.signatureCount()) + " routines");
            sig.dump();
            return 0;
        }
        const string exePath = path1, mapPath = path2, outPath = path3;
        if (exePath.empty()) fatal("No executable/signature file path not provided");
        if (mapPath.empty()) fatal("Map file path not provided");
        if (outPath.empty()) fatal("Output file path not provided");
        if (!checkFile(exePath).exists) fatal("Executable file does not exist: " + exePath);
        if (!checkFile(mapPath).exists) fatal("Map file does not exist: " + mapPath);
        if (!overwrite && checkFile(outPath).exists) fatal("Output file already exists: " + outPath);
        CodeMap map{mapPath, loadSegment, CodeMap::MAP_MZRE};
        info("Loaded map file " + mapPath + ": " + to_string(map.segmentCount()) + " segments, " + to_string(map.routineCount()) + " routines, " + to_string(map.variableCount()) + " variables");
        MzImage mz{exePath, loadSegment};
        Executable exe{mz};
        SignatureLibrary sig{map, exe, minInstructions, maxInstructions};
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