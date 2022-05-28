#include <iostream>
#include "dos/dos.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

void usage() {
    cout << "Usage: psptool <psp_image_file>" << endl;
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) usage();
    const char* pspfile = argv[1];
    try {
        ProgramSegmentPrefix psp;
        if (!readBinaryFile(string(pspfile), reinterpret_cast<char*>(&psp))) {
            cout << "Unable to read psp data from " << pspfile << endl;
        }
        cout << psp << endl;
    }
    catch (Error &e) {
        cout << "Exception: " << e.what() << endl;
    }
    return 0;
}