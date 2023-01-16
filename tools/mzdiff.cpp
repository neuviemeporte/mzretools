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

using namespace std;

void usage() {
    cout << "usage: mzdiff <base.exe> <compare.exe>" << endl;
    exit(1);
}

void fatal(const string &msg) {
    cout << "ERROR: " << msg << endl;
    exit(1);
}

string mzInfo(const MzImage &mz) {
    ostringstream str;
    str << mz.path() << ", load module of size " <<  mz.loadModuleSize() << " at " << hexVal(mz.loadModuleOffset()) 
        << ", entrypoint at " << mz.entrypoint() << ", stack at " << mz.stackPointer();
    return str.str();
}

void compare(const MzImage &base, const MzImage &compare) {

}

int main(int argc, char *argv[]) {
    setOutputLevel(LOG_DEBUG);
    if (argc != 3) {
        usage();
    }
    const string pathBase{argv[1]}, pathCompare{argv[2]};
    for (const auto &path : vector<string>{ pathBase, pathCompare }) {
        const auto stat = checkFile(path);
        if (!stat.exists) fatal("File does not exist: "s + path);
        else if (stat.size <= MZ_HEADER_SIZE) fatal("File too small ("s + to_string(stat.size) + "B): " + path); 
    }
    try {
        MzImage mzBase{pathBase};
        cout << "Base executable: " << mzInfo(mzBase) << endl;
        MzImage mzCompare{pathCompare};
        cout << "Compare executable: " << mzInfo(mzCompare) << endl;
        mzBase.load(0);
        mzCompare.load(0);
        compare(mzBase, mzCompare);
    }
    catch (Error &e) {
        fatal(e.why());
    }
    catch (...) {
        fatal("Unknown exception");
    }
    return 0;
}