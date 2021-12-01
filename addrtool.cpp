#include <iostream>
#include <string>
#include <regex>
#include <iomanip>
#include "memory.h"
#include "types.h"

using namespace std;

int main() {
    string line;
    smatch match;
    const regex segmentedRe("([0-9A-Fa-f]+):([0-9A-Fa-f]+)");
    cout << "Please provide addresses, empty line to quit: " << endl;
    while (true) {
        cout << "> ";
        cin >> line;
        if (line.empty()) {
            return 0;
        }
        else if (regex_match(line, match, segmentedRe)) {
            istringstream segStr(match.str(1)), ofsStr(match.str(2));
            Word seg, ofs;
            segStr >> hex >> seg;
            ofsStr >> hex >> ofs;
            SegmentedAddress addr(seg, ofs);
            cout << "Segmented address 0x" << hex << seg << ":" << ofs << " -> linear 0x" << addr.toLinear();
            addr.normalize();
            cout << ", normalized: 0x" << hex << addr.segment << ":" << addr.offset << ", linear 0x" << addr.toLinear() << endl;
        }
        else {
            istringstream addrStr(line);
            Offset linear;
            addrStr >> hex >> linear;
            SegmentedAddress addr(linear);
            cout << "Linear address 0x" << hex << linear << " -> 0x" << addr.segment << ":" << addr.offset;
            addr.normalize();
            cout << ", normalized: 0x" << hex << addr.toLinear() << " -> 0x" << addr.segment << ":" << addr.offset << endl;
        }
    }
    return 0;
}