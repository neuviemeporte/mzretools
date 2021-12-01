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

std::istream& safeGetline(std::istream& is, std::string& t) {
    t.clear();

    // The characters in the stream are read one-by-one using a std::streambuf.
    // That is faster than reading them one-by-one using the std::istream.
    // Code that uses streambuf this way must be guarded by a sentry object.
    // The sentry object performs various tasks,
    // such as thread synchronization and updating the stream state.

    std::istream::sentry se(is, true);
    std::streambuf* sb = is.rdbuf();

    for(;;) {
        int c = sb->sbumpc();
        switch (c) {
        case '\n':
            return is;
        case '\r':
            if(sb->sgetc() == '\n')
                sb->sbumpc();
            return is;
        case std::streambuf::traits_type::eof():
            // Also handle the case when the last line has no line ending
            if(t.empty())
                is.setstate(std::ios::eofbit);
            return is;
        default:
            t += (char)c;
        }
    }
}