#ifndef UTIL_H
#define UTIL_H

#include <istream>
#include <sstream>
#include <string>
#include <iomanip>
#include <vector>
#include <regex>

#include "dos/types.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

void hexDump(std::ostream &str, const Byte *buf, const Size size, const Size off = 0, const bool header = true);
void hexDump(const Byte *buf, const Size size, const Size off = 0, const bool header = true);

template<typename T, Size size = sizeof(T)> std::string hexString(const T &obj) {
    const Byte *buf = reinterpret_cast<const Byte*>(&obj);
    std::ostringstream str;
    str << std::hex << std::setw(2);
    for (Size i = 0; i < size; ++i)
        str << static_cast<int>(buf[i]);
    return str.str();
}

template<typename T> std::string hexJoin(const std::vector<T> &items) {
    std::ostringstream str;
    str << "[ ";
    auto it = items.begin();
    for (; it < items.end() - 1; ++it) 
        str << "0x" << std::hex << *it << ", ";
    str << "0x" << std::hex << *(it) << " ]";
    return str.str();
}

std::string signedHexVal(const SByte val, bool plus = true);
std::string signedHexVal(const SWord val, bool plus = true);

std::string hexVal(const Byte  val, bool prefix = true, bool pad = true);
std::string hexVal(const SByte val, bool prefix = true, bool pad = true);
std::string hexVal(const Word  val, bool prefix = true, bool pad = true);
std::string hexVal(const SWord val, bool prefix = true, bool pad = true);
std::string hexVal(const DWord val, bool prefix = true, bool pad = true);
std::string hexVal(const Offset val, const bool hdr = true, const int pad = 0);
std::string hexVal(SOffset val, const bool hdr = true, const int pad = 0);
std::string hexVal(const void* ptr);
std::string sizeStr(const Size s);
std::string ratioStr(const Size p, const Size q);
std::istream& safeGetline(std::istream& is, std::string& t);

struct FileStatus {
    bool exists;
    size_t size;
};

FileStatus checkFile(const std::string &path);
std::string getDirname(const std::string &path);
std::string getExtension(const std::string &path);
std::string replaceExtension(const std::string &path, const std::string &ext);
bool deleteFile(const std::string &path);
bool readBinaryFile(const std::string &path, Byte *buf, const Size size = 0);
void writeBinaryFile(const std::string &path, const Byte *buf, const Size size);
std::string binString(const Word &value);
std::string binString(const DWord &value);
std::string bytesToHex(const std::vector<Byte> &bytes);
std::vector<SWord> hexaToNumeric(const std::string &hexa);
std::string numericToHexa(const ByteString &pattern);
std::vector<std::string> splitString(const std::string &str, char delim);
void erasePattern(ByteString &str, const ByteString &pat);
bool regexMatch(const std::regex &re, const std::string &str);
std::vector<std::string> extractRegex(const std::regex &re, const std::string &str);

#endif // UTIL_H
