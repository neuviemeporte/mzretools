#include <iostream>
#include <fstream>
#include <iomanip>
#include <bitset>
#include <unistd.h>
#include <sys/stat.h>

#include "dos/util.h"

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

std::string hexVal(const Byte val) {
    std::ostringstream str;
    str << "0x" << std::hex << std::setw(2) << std::setfill('0') <<  (int)val;
    return str.str();
}

std::string hexVal(const Word val) {
    std::ostringstream str;
    str << "0x" << std::hex << std::setw(4) << std::setfill('0') <<  val;
    return str.str();    
}

std::string hexVal(const Offset val) {
    std::ostringstream str;
    str << "0x" << std::hex << val;
    return str.str();        
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

FileStatus checkFile(const std::string &path) {
    struct stat statbuf;
    int error = stat(path.c_str(), &statbuf);
    FileStatus ret;
    ret.exists = error == 0;
    ret.size = static_cast<Size>(statbuf.st_size);
    return ret;
}

bool deleteFile(const std::string &path) {
    if (path.empty()) 
        return false;
    return unlink(path.c_str()) == 0;
}

bool readBinaryFile(const std::string &path, Byte *buf, const Size size) {
    auto status = checkFile(path);
    if (!status.exists || status.size == 0) {
        cout << "File " << path << " does not exist or has size zero!";
        return false;
    }
    if (size && status.size < size) {
        cout << "File " << path << " has too small size (" << status.size << ") to read " << size << " bytes!";
        return false;
    }

    const Size 
        block_size = 512,
        total_size = size ? size : status.size,
        remainder = total_size % block_size,
        block_count = remainder == 0 ? total_size / block_size : (total_size / block_size) + 1;

    ifstream file{path, ios::binary};
    for (Size i = 1; i <= block_count; ++i) {
        const Size read_size = remainder == 0 || i != block_count ? block_size : remainder;
        if (!file.read(reinterpret_cast<char*>(buf), read_size)) {
            cout << "Unable to read " << read_size << " bytes, block " << i << ", block count = " << block_count << ", remainder = " << remainder << endl;
            return false;
        }
    }

    return true;
}

void writeBinaryFile(const std::string &path, const Byte *buf, const Size size) {
    ofstream file{path, ios::binary};
    file.write(reinterpret_cast<const char*>(buf), size);
}

std::string binString(const Word &value) {
    bitset<16> bits{value};
    return bits.to_string();
}