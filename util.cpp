#include "util.h"

#include <iostream>
#include <iomanip>

using namespace std;

void hexDump(const Byte *buf, const Size size, const Size off, const bool header)
{
    if (header) cout << std::hex << std::setfill('0') << "buf[0x" << size
         << "] @ 0x" << reinterpret_cast<size_t>(buf)
         << " + 0x" << off << ": " << endl;
    if (!buf || !size || off >= size) return;
    const size_t bpl = 16; // display bytes per line
    const size_t limit = 50 * bpl;
    if (size - off > 2 * limit) {
        hexDump(buf, limit, 0, false);
        cout << "[...]" << endl;
        hexDump(buf, size, size - limit, false);
        return;
    }
    for (size_t i = 0; i + off < size; ++i) {
        const size_t pos = i % bpl;
        if (pos == 0) { // header at line start
            cout << "0x" << std::setw(8) << i + off << ": ";
        }
        // hex byte
        cout << std::setw(2) << static_cast<int>(buf[i + off]) << " ";
        if (pos + 1 == bpl || i + 1 == size) { // ascii dump at line or buffer end
            for (size_t j = 1; j <= bpl; ++j) {
                if (pos + j < bpl) { cout << "   "; continue; }
                const unsigned char c = buf[i + off - bpl + j];
                if (c >= 0x20 && c <= 0x7e) cout << c;
                else cout << ".";
            }
            cout << endl;
        }
    }
}
