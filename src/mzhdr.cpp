#include <iostream>
#include <iomanip>
#include <string>
#include "dos/mz.h"
#include "dos/error.h"
#include "dos/output.h"

using namespace std;

void usage() {
    cout << "mzhdr v" << VERSION << endl
         << "Usage: mzhdr <mzfile> [-l|-s|-p seg outfile]" << endl
         << "-l                 only print offset of load module" << endl
         << "-s                 only print size of load module" << endl
         << "-p seg outfile     patch executable relocations to seg and dump load module to outfile" << endl;
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) usage();
    const char* mzfile = argv[1];
    try {
        MzImage mz{string(mzfile)};
        if (argc == 2) cout << mz.dump() << endl;
        else { // argc > 2
            string opt{argv[2]};
            if (opt == "-l") {
                if (argc != 3) throw ArgError("-l does not take argments");
                cout << "0x" << hex << mz.headerLength() << endl;
            }
            else if (opt == "-s") {
                if (argc != 3) throw ArgError("-s does not take arguments");
                cout << "0x" << hex << mz.loadModuleSize() << endl;
            }
            else if (opt == "-p") {
                if (argc != 5) throw ArgError("-p takes two arguments");
                string segStr{argv[3]}, outFile{argv[4]};
                const Word loadSeg = static_cast<Word>(stoi(segStr, nullptr, 0));
                mz.load(loadSeg);
                mz.writeLoadModule(outFile);
            }
            else throw ArgError("Unrecognized option: "s + opt);
        }
    }
    catch (Error &e) {
        cout << "Error: " << e.what() << endl;
        return 1;
    }
    return 0;
}