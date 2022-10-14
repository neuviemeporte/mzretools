#include <iostream>
#include "dos/mz.h"
#include "dos/error.h"

using namespace std;

void usage() {
    cout << "Usage: mzhdr <mzfile>" << endl;
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) usage();
    const char* mzfile = argv[1];
    try {
        MzImage mz{string(mzfile)};
        cout << mz.dump() << endl;
    }
    catch (Error &e) {
        cout << "Exception: " << e.what() << endl;
    }
    return 0;
}