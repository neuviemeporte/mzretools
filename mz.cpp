#include <fstream>
#include <string>
#include <sys/stat.h>
#include <string.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <cassert>
#include <cstddef>

#include "mz.h"
#include "error.h"
#include "util.h"

using namespace std;

MzImage::MzImage(const std::string &path) :
    path_(path) {
    if (path_.empty()) 
        throw ArgError("Empty path for MzImage!");
    const char* cpath = path.c_str();
    assert(cpath != nullptr);
    const auto file = checkFile(path);
    if (!file.exists) {
        ostringstream msg("MzImage file ", std::ios_base::ate);
        msg << path << " does not exist";
        throw IoError(msg.str());
    }

    filesize_ = file.size;
    if (filesize_ < HEADER_SIZE)
        throw IoError(string("MzImage file too small (") + to_string(filesize_) + ")!");

    // parse MZ header
    // TODO: use fstream and ios::binary
    FILE *mzFile = fopen(path_.c_str(), "r");
    if (!mzFile)
        throw IoError("Unable to open file "s + path_);
    auto totalSize = fread(&header_, 1, HEADER_SIZE, mzFile);
    if (totalSize != HEADER_SIZE) {
        fclose(mzFile);
        throw IoError("Incorrect read size from MzImage file: "s + to_string(totalSize));
    }
    if (header_.signature != SIGNATURE) {
        ostringstream msg("MzImage file has incorrect signature (0x", std::ios_base::ate);
        msg << std::hex << header_.signature << ")!";
        fclose(mzFile);
        throw IoError(msg.str());
    }

    // read in relocation entries
    if (header_.num_relocs) {
        if (fseek(mzFile, header_.reloc_table_offset, SEEK_SET) != 0) {
            fclose(mzFile);
            throw IoError("Unable to seek to relocation table!");
        }
        for (size_t i = 0; i < header_.num_relocs; ++i) {
            Relocation reloc;
            auto relocReadSize = fread(&reloc, 1, RELOC_SIZE, mzFile);
            if (relocReadSize != RELOC_SIZE) {
                fclose(mzFile);
                throw IoError("Invalid relocation read size from MzImage file: "s + to_string(relocReadSize));
            }
            relocs_.push_back(reloc);
        }
    }

    // calculate load module offset
    loadModuleOffset_ = header_.header_paragraphs * PARAGRAPH;
    loadModuleSize_ = filesize_ - loadModuleOffset_;
    // store original values at relocation offsets
    for (const auto &reloc : relocs_) {
        SegmentedAddress relocAddr(reloc.segment, reloc.offset);
        Offset fileOffset = relocAddr.toLinear() + loadModuleOffset_;
        if (fseek(mzFile, fileOffset, SEEK_SET) != 0) {
            fclose(mzFile);
            throw IoError("Unable to seek to relocation offset!");
        }
        Word relocVal;
        auto relocValSize = fread(&relocVal, 1, sizeof(Word), mzFile);
        if (relocValSize != sizeof(Word)) {
            fclose(mzFile);
            throw IoError("Invalid relocation value read size from MzImage file: "s + to_string(relocValSize));                
        }
        relocVals_.push_back(relocVal);
    }
    assert(relocs_.size() == relocVals_.size());

    fclose(mzFile);
}

std::string MzImage::dump() const {
    ostringstream msg;
    char signatureStr[sizeof(Word) + 1] = {0};
    memcpy(signatureStr, &header_.signature, sizeof(Word));
    msg << "--- " << path_ << " MZ header (" << std::dec << HEADER_SIZE << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, signature) << "] signature = 0x" << header_.signature << " ('" << signatureStr << "')" << endl
        << "\t[0x" << hex << offsetof(Header, last_page_size) << "] last_page_size = " << std::hex << "0x" << header_.last_page_size << endl       
        << "\t[0x" << hex << offsetof(Header, pages_in_file) << "] pages_in_file = " << std::dec << header_.pages_in_file 
            << " (" << header_.pages_in_file * PAGE << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, num_relocs) << "] num_relocs = " << std::dec << header_.num_relocs << endl
        << "\t[0x" << hex << offsetof(Header, header_paragraphs) << "] header_paragraphs = " << std::dec << header_.header_paragraphs 
            << " (" << header_.header_paragraphs * PARAGRAPH << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, min_extra_paragraphs) << "] min_extra_paragraphs = " << std::dec << header_.min_extra_paragraphs 
            << " (" << header_.min_extra_paragraphs * PARAGRAPH << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, max_extra_paragraphs) << "] max_extra_paragraphs = " << std::dec << header_.max_extra_paragraphs << endl
        << "\t[0x" << hex << offsetof(Header, ss) << "] ss:sp = " << std::hex << header_.ss << ":" << header_.sp << endl
        << "\t[0x" << hex << offsetof(Header, checksum) << "] checksum = " << std::hex << "0x" << header_.checksum << endl
        << "\t[0x" << hex << offsetof(Header, cs) << "] cs:ip = " << std::hex << header_.cs << ":" << header_.ip << endl
        << "\t[0x" << hex << offsetof(Header, reloc_table_offset) << "] reloc_table_offset = " << std::hex << "0x" << header_.reloc_table_offset << endl
        << "\t[0x" << hex << offsetof(Header, overlay_number) << "] overlay_number = " << std::dec << header_.overlay_number << endl;

    if (relocs_.size()) {
        msg << "--- relocations: " << endl;
        size_t i = 0;
        for (const auto &r : relocs_) {
            SegmentedAddress a(r.segment, r.offset);
            a.normalize();
            msg << "\t[" << std::dec << i << "]: " << std::hex << r.segment << ":" << r.offset 
                << ", linear: 0x" << a.toLinear() 
                << ", file offset: 0x" << a.toLinear() + loadModuleOffset_ 
                << ", file value = 0x" << relocVals_[i]
                << endl;
            i++;
        }
    }

    msg << "--- load module @ " << hex << "0x" << loadModuleOffset_ 
        << ", size = 0x" << loadModuleSize_ << " / " << dec << loadModuleSize_ << " bytes" << endl;
    return msg.str();
}

void MzImage::loadMap(const std::string &path) {
    const char* cpath = path.c_str();
    struct stat statbuf;
    if (stat(cpath, &statbuf) != 0) {
        ostringstream msg("Unable to stat IDA map file ", std::ios_base::ate);
        msg << path << " (" << strerror(errno) << ")";
        throw IoError(msg.str());
    }

    ifstream mapFile(path);
    string line;
    // IDA map file consists of a section for segment definitions and publics definitions, 
    // plus an additional entrypoint info at the end
    const regex 
        segmentsStartRe("\\s*Start\\s+Stop\\s+Length\\s+Name\\s+Class"),
        segmentRe("\\s*([0-9A-F]+)H\\s+([0-9A-F]+)H\\s+([0-9A-F]+)H\\s+([a-zA-Z0-9_]+)\\s+([A-Z]+)"),
        publicsStartRe("\\s*Address\\s+Publics by Value"),
        publicRe("\\s*([0-9A-F]+):([0-9A-F]+)\\s+([a-zA-Z0-9_]+)"),
        entrypointRe("\\s*Program entry point at ([0-9A-F]+):([0-9A-F]+)");
    size_t lineno = 0;
    std::smatch match;
    enum MapSection { MAP_NONE, MAP_SEGS, MAP_PUBLICS } mapSection = MAP_NONE;
    while (safeGetline(mapFile, line)) {
        lineno++;
        if (line.empty())
            continue;
        else if (regex_match(line, segmentsStartRe)) {
            cout << std::dec << lineno << ": segments '" << line << "'" << endl;
            mapSection = MAP_SEGS;
        }
        else if (regex_match(line, publicsStartRe)) {
            cout << std::dec << lineno << ": publics '" << line << "'" << endl;
            mapSection = MAP_PUBLICS;
        }
        else if (regex_match(line, match, entrypointRe)) {
            assert(match.size() == 3);
            istringstream segStr(match.str(1)), ofsStr(match.str(2));
            Word seg, ofs;
            if (segStr >> hex >> seg && ofsStr >> hex >> ofs) {
                entrypoint_ = { seg, ofs };
                cout << "Parsed entrypoint: " << entrypoint_ << endl;
            }
            else throw ParseError("Invalid entrypoint at line "s + to_string(lineno) + ": " + line);
            mapSection = MAP_NONE;
        }
        else if (mapSection == MAP_SEGS) {
            std::smatch match;
            Dword start, stop, length;
            string name, type;
            if (regex_match(line, match, segmentRe) && match.size() == 6 
                && istringstream(match.str(1)) >> hex >> start
                && istringstream(match.str(2)) >> hex >> stop
                && istringstream(match.str(3)) >> hex >> length
                && istringstream(match.str(4)) >> name
                && istringstream(match.str(5)) >> type) {
                    Segment seg(name, type, start, stop);
                    cout << "Parsed segment: " << seg << endl;
                    if (seg.length() != length) {
                        ostringstream msg("Invalid segment length in map file (0x", std::ios_base::ate);
                        msg << std::hex << length << " vs calculated 0x" << seg.length() << " at line " << lineno;
                        throw ParseError(msg.str());
                    }
                    segs_.push_back(seg);
            }
            else throw ParseError("Invalid segment definition at line "s + to_string(lineno) + ": " + line);
        }
        else if (mapSection == MAP_PUBLICS) {
            std::smatch match;
            Word seg, ofs;
            string name;
            if (regex_match(line, match, publicRe) && match.size() == 4
                && istringstream(match.str(1)) >> hex >> seg
                && istringstream(match.str(2)) >> hex >> ofs
                && istringstream(match.str(3)) >> name) {
                    Public pub{seg, ofs, name};
                    cout << "Parsed public: " << pub << endl;
                    publics_.push_back(pub);
            }
            else throw ParseError("Invalid public definition at line "s + to_string(lineno) + ": " + line);
        }
        else throw ParseError("Unrecognized syntax at line "s + to_string(lineno) + ": " + line);
    }
}

MzImage::Segment::Segment(const std::string &name, const std::string &type, Dword start, Dword stop) :
    name(name), type(type), start(start), stop(stop) {
    if (start > stop) {
        ostringstream msg("Unable to create Segment: start (0x", std::ios_base::ate);
        msg << std::hex << start << ") > stop (0x" << stop << ")";
        throw LogicError(msg.str());
    }
}

std::ostream& operator<<(std::ostream &os, const MzImage::Segment &arg) {
    return os << arg.name << " [" << arg.type << "]: 0x" << std::hex << arg.start << "-" << arg.stop << " (0x" << arg.length() << ")";
}

std::ostream& operator<<(std::ostream &os, const MzImage::Public &arg) {
    return os << arg.name << ": " << arg.addr;
}
