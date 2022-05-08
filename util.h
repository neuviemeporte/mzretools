#ifndef UTIL_H
#define UTIL_H

#include <istream>
#include <sstream>
#include <string>
#include <iomanip>

#include "types.h"

void hexDump(const Byte *buf, const Size size, const Size off = 0, const bool header = true);
template<typename T, Size size = sizeof(T)> std::string hexString(const T &obj) {
    const Byte *buf = reinterpret_cast<const Byte*>(&obj);
    std::ostringstream str;
    str << std::hex << std::setw(2);
    for (Size i = 0; i < size; ++i)
        str << static_cast<int>(buf[i]);
    return str.str();    
}
template<typename T> std::string hexVal(const T &val) {
    std::ostringstream str;
    str << "0x" << std::hex << val;
    return str.str();
}
std::istream& safeGetline(std::istream& is, std::string& t);

struct FileStatus {
    bool exists;
    size_t size;
};

FileStatus checkFile(const std::string &path);
bool deleteFile(const std::string &path);
bool readBinaryFile(const std::string &path, char *buf, const Size size = 0);

#endif // UTIL_H
