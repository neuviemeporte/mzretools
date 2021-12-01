#ifndef UTIL_H
#define UTIL_H

#include <istream>
#include <string>

#include "types.h"

void hexDump(const Byte *buf, const Size size, const Size off = 0, const bool header = true);
std::istream& safeGetline(std::istream& is, std::string& t);

#endif // UTIL_H
