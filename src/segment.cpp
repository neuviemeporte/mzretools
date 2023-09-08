#include "dos/segment.h"

#include <regex>

#include "dos/error.h"
#include "dos/util.h"

using namespace std;

Segment::Segment(const std::string &str) : type(SEG_NONE) {
    static const regex 
        CODE_RE{"^CODE/0x([0-9a-fA-F]{1,4})"}, 
        DATA_RE{"^DATA/0x([0-9a-fA-F]{1,4})"},
        STACK_RE{"^STACK/0x([0-9a-fA-F]{1,4})"};
    
    smatch match;
    if (regex_match(str, match, CODE_RE)) type = SEG_CODE;
    else if (regex_match(str, match, DATA_RE)) type = SEG_DATA;
    else if (regex_match(str, match, STACK_RE)) type = SEG_STACK;

    const string valstr = match.str(1);
    if (type != SEG_NONE) value = stoi(valstr, nullptr, 16);
    else throw ArgError("Invalid segment string: " + str);
}

std::string Segment::toString() const {
    ostringstream str;
    switch (type) {
    case SEG_CODE:  str << "CODE/"; break;
    case SEG_DATA:  str << "DATA/"; break;
    case SEG_STACK: str << "STACK/"; break;
    default:        str << "???""/"; break; 
    }
    str << hexVal(value);
    return str.str();
}
