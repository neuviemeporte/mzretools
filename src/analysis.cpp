#include <iostream>
#include <sstream>
#include "dos/analysis.h"

using namespace std;

bool Routine::contains(const Address &addr) const {
    if (extents.contains(addr)) return true;
    for (const auto &c : chunks) {
        if (c.contains(addr)) return true;
    }
    return false;
}

string Routine::toString() const {
    ostringstream str;
    str << extents.toString(true) << ": " << name << " / " << to_string(id) << endl;
    for (const auto &c : chunks)
        str << "\tfunction chunk: " << c.toString(true) << endl;
    return str.str();
}

void Analysis::dump() const {
    // display routines
    for (const auto &r : routines) {
        cout << r.toString();
    }
}