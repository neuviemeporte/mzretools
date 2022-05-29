#include <iostream>
#include <iomanip>
#include <sstream>
#include "dos/address.h"
#include "dos/error.h"
#include "dos/util.h"

using namespace std;

Address::Address(const Offset linear) {
    if (linear >= MEM_TOTAL) throw MemoryError("Linear address too big while converting to segmented representation: "s + hexVal(linear));
    segment = static_cast<Word>(linear >> 4);
    offset = static_cast<Word>(linear & 0xf);
}

Address::operator std::string() const {
    ostringstream str;
    str << *this;
    return str.str();
}

std::string Address::toString() const {
    return static_cast<string>(*this);
}

void Address::normalize() {
    segment += offset >> 4;
    offset &= 0xf;
}

std::ostream& operator<<(std::ostream &os, const Address &arg) {
    return os << std::hex << std::setw(4) << std::setfill('0') << arg.segment 
        << ":" << std::setw(4) << std::setfill('0') << arg.offset << " / 0x" << arg.toLinear();
}