#include "dos/output.h"
#include "dos/analysis.h"
#include "dos/error.h"
#include "dos/executable.h"
#include "dos/util.h"

using namespace std;

OUTPUT_CONF(LOG_SYSTEM)

// TODO: allow to specify address ranges/var name pattern to ignore? could help against finding libc/crt stuff
void usage() {
    ostringstream str;
    str << "mzptr v" << VERSION << endl
        << "Usage: mzptr [options] exe_file exe_map" << endl
        << "Searches the executable for locations where offsets to known data objects could potentially be stored." << endl
        << "Results will be written to standard output; these should be reviewed and changed to references to variables "
        << "during executable reconstruction. " << endl
        << "Options:" << endl
        << "--verbose:       show more detailed information about processed routines" << endl
        << "--debug:         show additional debug information";
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
    Word loadSegment = 0x1000;
    string exePath, mapPath;
    for (int aidx = 1; aidx < argc; ++aidx) {
        string arg(argv[aidx]);
        if (arg == "--debug") setOutputLevel(LOG_DEBUG);
        else if (arg == "--verbose") { setOutputLevel(LOG_VERBOSE); }
        else if (exePath.empty()) exePath = arg;
        else if (mapPath.empty()) mapPath = arg;
        else fatal("Unrecognized argument: "s + arg);
    }
    try {
        MzImage mz{exePath, loadSegment};
        Executable exe{mz};
        CodeMap map{mapPath, loadSegment};
        if (map.codeSize() != mz.loadModuleSize()) throw LogicError("Map size " + sizeStr(map.codeSize()) + " does not match size of executable load module: " + sizeStr(mz.loadModuleSize()));        
        verbose("Loaded executable of size " + sizeStr(mz.loadModuleSize()) + ", parsed map of size " + sizeStr(map.codeSize()));
        Analyzer::Options options;
        Analyzer a{options};
        a.findDataRefs(exe, map);
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