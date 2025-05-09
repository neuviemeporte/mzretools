#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <bitset>
#include <unistd.h>
#include <sys/stat.h>

#include "dos/util.h"
#include "dos/output.h"
#include "dos/error.h"
#include "dos/types.h"

using namespace std;
namespace fs = std::filesystem;

inline bool printable(const Byte b) { return b >= 0x20 && b <= 0x7e; }

void hexDump(ostream &str, const Byte *buf, const Size size, const Size off, const bool header) {
    if (header) str << std::hex << std::setfill('0') << "buf[0x" << size
         << "] @ 0x" << reinterpret_cast<size_t>(buf)
         << " + 0x" << off << ": " << endl;
    if (!buf) {
        output("(null buffer)", LOG_OTHER, LOG_ERROR);
        return;
    }
    if (!size) return;
    const size_t bpl = 16; // display bytes per line
    const size_t limit = 50 * bpl; 
    if (size > 2 * limit) {
        hexDump(buf, limit, 0, false);
        output("[...]", LOG_OTHER, LOG_INFO);
        hexDump(buf, size, size - limit, false);
        return;
    }
    for (size_t i = 0; i < size; ++i) {
        const size_t pos = i % bpl;
        if (pos == 0) { // header at line start
            str << "0x" << std::hex << std::setfill('0') << std::setw(8) << i + off << ": ";
        }
        // hex byte
        str << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(buf[i]) << " ";
        if (pos + 1 == bpl || i + 1 == size) { // ascii dump at line or buffer end
            for (size_t j = 1; j <= bpl; ++j) {
                if (pos + j < bpl) { str << "   "; continue; }
                const unsigned char c = buf[i - bpl + j];
                if (printable(c)) str << c;
                else str << ".";
            }
            str << endl;
        }
    }
}

void hexDump(const Byte *buf, const Size size, const Size off, const bool header) {
    ostringstream str;
    hexDump(str, buf, size, off, header);
    output(str.str(), LOG_OTHER, LOG_INFO, OUT_DEFAULT, true);
}

void hexDiff(const Byte *buf1, const Byte *buf2, const Offset start, const Offset end, const Word buf1seg, const Word buf2seg) {
    const Size hexDumpItems = 16;
    ostringstream oss, buf1hex, buf1asc, buf2hex, buf2asc;
    Offset newlineOff = start;
    for (Offset off = start; off <= end + 1; ++off) {
        const Offset fromStart = start - off;
        const Size rowRemainder = fromStart % hexDumpItems;
        bool newline = false;
        // check for new line condition, output data gathered from previous row if present
        if (fromStart && (rowRemainder == 0 || off > end)) { 
            newline = true;
            // take care of padding on the last row
            if (rowRemainder != 0) for (Size i = 0; i < rowRemainder; ++i) {
                buf1hex << "   ";
                buf2hex << "   ";
                buf1asc << " ";
                buf2asc << " ";
            }
            oss << std::hex << std::setfill('0') << std::setw(4) << buf1seg << ":" << std::setw(4) << newlineOff
                << buf1hex.str() << '|' << buf1asc.str() << "| "
                << std::setw(4) << buf2seg << ":" << std::setw(4) << newlineOff
                << buf2hex.str() << '|' << buf2asc.str() << '|' << endl;
            buf1hex.str("");
            buf1hex.clear();
            buf1asc.str("");
            buf1asc.clear();
            buf2hex.str("");
            buf2hex.clear();
            buf2asc.str("");
            buf2asc.clear();
            newlineOff += hexDumpItems;
            // need the one iteration past end to output the final line AFTER grabbing the last byte, but break immediately after
            if (off > end) break;
        }
        // collect data for hexdumps
        const Byte b1 = buf1[off], b2 = buf2[off];
        if (b1 != b2) {
            buf1hex << output_color(OUT_RED); 
            buf1asc << output_color(OUT_RED);
            buf2hex << output_color(OUT_RED); 
            buf2asc << output_color(OUT_RED);
        }
        buf1hex << (newline || !fromStart ? '|' : ' ') << std::hex << std::setfill('0') << std::setw(2) << (int)b1;
        buf2hex << (newline || !fromStart ? '|' : ' ') << std::hex << std::setfill('0') << std::setw(2) << (int)b2;
        buf1asc << (printable(b1) ? (char)b1 : '.');
        buf2asc << (printable(b2) ? (char)b2 : '.');
        if (b1 != b2) {
            buf1hex << output_color(OUT_DEFAULT);
            buf1asc << output_color(OUT_DEFAULT);
            buf2hex << output_color(OUT_DEFAULT);
            buf2asc << output_color(OUT_DEFAULT);
        }
    }
    output(oss.str(), LOG_OTHER, LOG_INFO, OUT_DEFAULT, true);
}

std::string signedHexVal(const SByte val, bool plus) {
    ostringstream str;
    if (val >= 0) {
        if (plus) str << "+";
        str << "0x" << hex << setw(2) << setfill('0') << (int)(val);
    }
    else str << "-0x" << hex << setw(2) << setfill('0') << (int)(~val + 1);
    return str.str();
}

std::string signedHexVal(const SWord val, bool plus) {
    ostringstream str;
    if (val >= 0) {
        if (plus) str << "+";
        str << "0x" << hex << setw(4) << setfill('0') << val;
    }
    else str << "-0x" << hex << setw(4) << setfill('0') << ~val + 1;
    return str.str();
}

// TODO: templatize
std::string hexVal(const Byte val, bool prefix, bool pad) {
    std::ostringstream str;
    if (prefix) str << "0x";
    if (pad) str << std::setw(2) << std::setfill('0');
    str << std::hex << (int)val;
    return str.str();
}

std::string hexVal(const SByte val, bool prefix, bool pad) {
    std::ostringstream str;
    if (prefix) str << "0x";
    if (pad) str << std::setw(2) << std::setfill('0');
    str << std::hex << (int)val;
    return str.str();
}

std::string hexVal(const Word val, bool prefix, bool pad) {
    std::ostringstream str;
    if (prefix) str << "0x";
    if (pad) str << std::setw(4) << std::setfill('0');
    str << std::hex <<  val;
    return str.str();    
}

std::string hexVal(const SWord val, bool prefix, bool pad) {
    std::ostringstream str;
    if (prefix) str << "0x";
    if (pad) str << std::setw(4) << std::setfill('0');
    str << std::hex << val;
    return str.str();    
}

std::string hexVal(const DWord val, bool prefix, bool pad) {
    std::ostringstream str;
    if (prefix) str << "0x";
    if (pad) str << std::setw(8) << std::setfill('0');
    str << std::hex <<  val;
    return str.str();    
}

std::string hexVal(const Offset val, const bool hdr, const int pad) {
    std::ostringstream str;
    if (hdr) str << "0x";
    if (pad) str << setfill('0') << setw(pad);
    str << hex <<  val;
    return str.str();
}

std::string hexVal(SOffset val, const bool hdr, const int pad) {
    std::ostringstream str;
    if (val < 0) { str << "-"; val = -val; }
    if (hdr) str << "0x";
    if (pad) str << setfill('0') << setw(pad);
    str << hex <<  val;
    return str.str();
}

std::string hexVal(const void* ptr) {
    ostringstream str;
    str << "0x" << hex << reinterpret_cast<uint64_t>(ptr);
    return str.str();
}

std::string sizeStr(const Size s) {
    ostringstream str;
    str << to_string(s) << "/" << hexVal(s);
    return str.str();
}

std::string ratioStr(const Size p, const Size q) {
    if (p == 0 || q == 0) return "0%"s;
    ostringstream str;
    str << to_string((p * 100)/q) << "%";
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

std::string getDirname(const std::string &path) {
    if (path.empty()) return path;
    fs::path p{path};
    p.remove_filename();
    return p.string();
}

std::string getExtension(const std::string &path) {
    if (path.empty()) return {};
    fs::path p{path};
    string ext = p.extension().string();
    if (ext.front() == '.') return string(ext.begin() + 1, ext.end());
    return {};
}

std::string replaceExtension(const std::string &path, const std::string &ext) {
    if (path.empty()) return path;
    fs::path p{path};
    p.replace_extension(ext);
    return p.string();
}

bool deleteFile(const std::string &path) {
    if (path.empty()) 
        return false;
    return unlink(path.c_str()) == 0;
}

bool readBinaryFile(const std::string &path, Byte *buf, const Size size) {
    auto status = checkFile(path);
    if (!status.exists || status.size == 0) {
        output("File "s + path + " does not exist or has size zero!", LOG_OTHER);
        return false;
    }
    if (size && status.size < size) {
        output("File "s + path + " has too small size (" + to_string(status.size) + ") to read " + to_string(size) + " bytes!", LOG_OTHER);
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
            output("Unable to read "s + to_string(read_size) + " bytes, block " + to_string(i) + ", block count = " + to_string(block_count) 
                + ", remainder = " + to_string(remainder), LOG_OTHER);
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

std::string binString(const DWord &value) {
    bitset<32> bits{value};
    return bits.to_string();
}

inline bool isHexDigit(const char d) {
    return (d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F');
}

ByteString hexaToNumeric(const std::string &hexa) {
    if (hexa.size() & 1) throw ArgError("Hexa string must be of even size");
    ByteString ret;
    for (Size s = 0; s < hexa.size(); s+=2) {
        const string byteStr = hexa.substr(s, 2);
        if (byteStr == "??") { ret.push_back(-1); continue; }
        ret.push_back(stoi(byteStr, nullptr, 16));
    }
    return ret; 
}

std::string bytesToHex(const std::vector<Byte> &bytes) {
    ostringstream str;
    for (Byte b : bytes) { 
        str << hexVal(b, false) << " ";
    }
    return str.str();
}

std::string numericToHexa(const ByteString &pattern) {
    ostringstream str;
    str << setbase(16) << setfill('0');
    for (const SWord byte : pattern) {
        if (byte < 0) str << "??";
        else if (byte > 0xff) throw ArgError("Invalid byte value in string: " + to_string(byte));
        else str << setw(2) << byte;
    }
    return str.str();
}

std::vector<string> splitString(const std::string &str, char delim) {
    vector<string> ret;
    size_t start = 0, end = 0;
    while (start < str.size() && (end = str.find(delim, start)) != string::npos) {
        ret.push_back(str.substr(start, end - start));
        start = end + 1;
    }
    ret.push_back(str.substr(start, str.size() - start));
    return ret;
}

void erasePattern(ByteString &str, const ByteString &pat) {
    if (pat.empty()) throw ArgError("Empty pattern for erasure");
    auto patFound = std::search(str.begin(), str.end(), pat.begin(), pat.end());
    if (patFound == str.end()) throw LogicError("Unable to find pattern in string for erasure");
    for (auto it = patFound; std::distance(patFound, it) < pat.size(); ++it) {
        *it = -1;
    }
}

std::vector<std::string> extractRegex(const std::regex &re, const std::string &str) {
    smatch match;
    if (!regex_match(str, match, re)) return {};
    vector<string> ret;
    for (int i = 1; i < match.size(); ++i) ret.push_back(match.str(i));
    return ret;
}