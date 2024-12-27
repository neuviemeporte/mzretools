#include <iostream>
#include <iomanip>
#include <string>
#include "dos/mz.h"
#include "dos/error.h"
#include "dos/output.h"

using namespace std;

void usage() {
    cout << "mzhdr v" << VERSION << endl
         << "Usage: mzhdr <mzfile> [-l]" << endl;
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) usage();
    const char* mzfile = argv[1];
    try {
        MzImage mz{string(mzfile)};
        if (argc == 2) cout << mz.dump() << endl;
        else {
            string opt{argv[2]};
            if (opt == "-l") {
                cout << "0x" << hex << mz.headerLength() << endl;
            }
            else throw ArgError("Unrecognized option: "s + opt);
        }
    }
    catch (Error &e) {
        cout << "Exception: " << e.what() << endl;
    }
    return 0;
}