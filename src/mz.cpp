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

#include "dos/mz.h"
#include "dos/error.h"
#include "dos/util.h"
#include "dos/output.h"

using namespace std;

OUTPUT_CONF(LOG_OS)

// only parse and read exe header and relocation table
MzImage::MzImage(const std::string &path) : path_(path), loadSegment_(0) {
    if (path_.empty()) 
        throw ArgError("Empty path for MZ file!");
    const auto file = checkFile(path);
    if (!file.exists)
        throw IoError("MZ file " + path + " does not exist");

    filesize_ = file.size;
    if (filesize_ < MZ_HEADER_SIZE)
        throw IoError("MZ file " + path + " too small: " + to_string(filesize_) + " bytes");

    // parse MZ header
    ifstream mzFile{path_, ios::binary};
    if (!mzFile)
        throw IoError("Unable to open file "s + path_);
    if (!mzFile.read(reinterpret_cast<char*>(&header_), MZ_HEADER_SIZE))
        throw IoError(("Unable to read header from file "s + path_));
    const auto hdrReadSize = mzFile.gcount();
    if (hdrReadSize != MZ_HEADER_SIZE)
        throw IoError("Incorrect read size for MZ header: "s + to_string(hdrReadSize));
    if (header_.signature != MZ_SIGNATURE)
        throw IoError("MZ executable file has incorrect signature: " + hexVal(header_.signature));

    // read in any bytes between end of header and beginning of relocation table (optional overlay information)
    debug("Relocation table at offset "s + hexVal(header_.reloc_table_offset) + ", header size = " + hexVal(MZ_HEADER_SIZE));
    if (header_.reloc_table_offset > MZ_HEADER_SIZE) {
        const Size ovlInfoSize = header_.reloc_table_offset - MZ_HEADER_SIZE;
        ovlinfo_ = vector<Byte>(ovlInfoSize);
        if (!mzFile.read(reinterpret_cast<char*>(ovlinfo_.data()), ovlInfoSize))
            throw IoError("Unable to read overlay info from "s + path_);
        const auto ovlReadSize = mzFile.gcount();
        if (ovlReadSize != ovlInfoSize) 
            throw IoError("Incorrect read size for overlay info: " + to_string(ovlReadSize));
    }

    // read in relocation entries
    if (header_.num_relocs) {
        if (!mzFile.seekg(header_.reloc_table_offset, ios::beg))
            throw IoError("Unable to seek to relocation table!");
        for (size_t i = 0; i < header_.num_relocs; ++i) {
            Word relocData[2];
            if (!mzFile.read(reinterpret_cast<char*>(&relocData), MZ_RELOC_SIZE))
                throw IoError("Unable to read data for relocation " + to_string(i));
            auto relocReadSize = mzFile.gcount();
            if (relocReadSize != MZ_RELOC_SIZE)
                throw IoError("Invalid relocation read size from MZ file: "s + to_string(relocReadSize));
            Relocation reloc;
            reloc.segment = relocData[1];
            reloc.offset = relocData[0];
            relocs_.push_back(reloc);
        }
    }

    // calculate load module offset
    loadModuleOffset_ = header_.header_paragraphs * PARAGRAPH_SIZE;
    if (header_.pages_in_file == 0)
        throw DosError("Page count in MZ header is zero");
    loadModuleSize_ = (header_.pages_in_file - 1) * PAGE_SIZE + header_.last_page_size - loadModuleOffset_;
    // store original values at relocation offsets
    for (auto &reloc : relocs_) {
        Address relocAddr(reloc.segment, reloc.offset);
        Offset fileOffset = relocAddr.toLinear() + loadModuleOffset_;
        if (!mzFile.seekg(fileOffset, ios::beg))
            throw IoError("Unable to seek to relocation offset " + hexVal(fileOffset));
        Word relocVal;
        if (!mzFile.read(reinterpret_cast<char*>(&relocVal), sizeof(relocVal)))
            throw IoError("Unable to read relocation value at offset " + hexVal(fileOffset));
        auto relocValSize = mzFile.gcount();
        if (relocValSize != sizeof(relocVal))
            throw IoError("Invalid relocation value read size from MZ file: "s + to_string(relocValSize));
        reloc.value = relocVal;
    }
    debug("Loaded MZ exe header from "s + path_ + ", entrypoint @ " + entrypoint().toString() + ", stack @ " + stackPointer().toString());
}

// parse header and load image into memory at specified segment
MzImage::MzImage(const std::string &path, const Word loadSegment) : MzImage(path) {
    load(loadSegment);
}

MzImage::MzImage(const std::vector<Byte> &code) : filesize_(0), loadModuleSize_(code.size()), loadModuleOffset_(0), entrypoint_(0, 0), loadSegment_(0) {
    std::copy(code.begin(), code.end(), std::back_inserter(loadModuleData_));
}

std::string MzImage::dump() const {
    ostringstream msg;
    char signatureStr[sizeof(Word) + 1] = {0};
    memcpy(signatureStr, &header_.signature, sizeof(Word));
    msg << "--- " << path_ << " MZ header (" << std::dec << MZ_HEADER_SIZE << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, signature) << "] signature = 0x" << header_.signature << " ('" << signatureStr << "')" << endl
        << "\t[0x" << hex << offsetof(Header, last_page_size) << "] last_page_size = " << std::hex << "0x" << header_.last_page_size
            << " (" << std::dec << header_.last_page_size << " bytes)" <<  endl       
        << "\t[0x" << hex << offsetof(Header, pages_in_file) << "] pages_in_file = " << std::dec << header_.pages_in_file 
            << " (" << header_.pages_in_file * PAGE_SIZE << " bytes, load module size "  << sizeStr(loadModuleSize()) << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, num_relocs) << "] num_relocs = " << std::dec << header_.num_relocs << endl
        << "\t[0x" << hex << offsetof(Header, header_paragraphs) << "] header_paragraphs = " << std::dec << header_.header_paragraphs 
            << " (" << headerLength() << " bytes, load module starts at " << hexVal(headerLength()) <<  ")" << endl
        << "\t[0x" << hex << offsetof(Header, min_extra_paragraphs) << "] min_extra_paragraphs = " << std::dec << header_.min_extra_paragraphs 
            << " (" << header_.min_extra_paragraphs * PARAGRAPH_SIZE << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, max_extra_paragraphs) << "] max_extra_paragraphs = " << std::dec << header_.max_extra_paragraphs << endl
        << "\t[0x" << hex << offsetof(Header, ss) << "] ss:sp = " << std::hex << header_.ss << ":" << header_.sp << endl
        << "\t[0x" << hex << offsetof(Header, checksum) << "] checksum = " << std::hex << "0x" << header_.checksum << endl
        << "\t[0x" << hex << offsetof(Header, cs) << "] cs:ip = " << std::hex << header_.cs << ":" << header_.ip << endl
        << "\t[0x" << hex << offsetof(Header, reloc_table_offset) << "] reloc_table_offset = " << std::hex << "0x" << header_.reloc_table_offset << endl
        << "\t[0x" << hex << offsetof(Header, overlay_number) << "] overlay_number = " << std::dec << header_.overlay_number;

    if (ovlinfo_.size() != 0) {
        msg << endl << "--- extra data (overlay info?)" << endl << "\t";
        size_t i = 0;
        for (auto &b : ovlinfo_) {
            if (i != 0) msg << ", ";
            msg << hexVal(b);
            i++;
        }
    }

    if (relocs_.size() != 0) {
        msg << endl << "--- relocations: " << endl;
        size_t i = 0;
        for (const auto &r : relocs_) {
            Address a(r.segment, r.offset);
            a.normalize();
            if (i != 0) msg << endl;
            msg << "\t[" << std::dec << i << "]: " << std::hex << r.segment << ":" << r.offset 
                << ", linear: 0x" << a.toLinear() 
                << ", file offset: 0x" << a.toLinear() + loadModuleOffset_ 
                << ", file value = 0x" << r.value;
            i++;
        }
    }

    msg << endl << "--- load module @ " << hex << "0x" << loadModuleOffset_ 
        << ", size = 0x" << loadModuleSize_ << " / " << dec << loadModuleSize_ << " bytes";
    return msg.str();
}

// read actual load module data
void MzImage::load(const Word loadSegment) {
    debug("Loading executable code: size = "s + hexVal(loadModuleSize_) + " bytes starting at file offset "s + hexVal(loadModuleOffset_) + ", relocation factor " + hexVal(loadSegment));
    ifstream mzFile{path_, ios::binary};
    if (!mzFile) 
        throw IoError("Unable to open MZ file: " + path_);
    if (!mzFile.seekg(loadModuleOffset_))
        throw IoError("Unable to seek to load module at offset " + hexVal(loadModuleOffset_));
    loadModuleData_ = vector<Byte>(loadModuleSize_, 0);
    loadSegment_ = loadSegment;
    if (!mzFile.read(reinterpret_cast<char*>(loadModuleData_.data()), loadModuleSize_))
        throw IoError("Error while reading load module data from "s + path_);
    const auto bytesRead = mzFile.gcount();
    if (bytesRead != loadModuleSize_) 
        throw IoError("Incorrect number of bytes read from "s  + path_ + ": " + to_string(bytesRead));
    // patch relocations
    for (const Relocation &r : relocs_) {
        const Address addr(r.segment, r.offset);
        const Offset off = addr.toLinear();
        const Word patchedVal = r.value + loadSegment;
#ifdef DEBUG        
        debug("Patching relocation at " + addr.toString() + " to " + hexVal(patchedVal));
#endif
        loadModuleData_[off] = lowByte(patchedVal);
        loadModuleData_[off + 1] = hiByte(patchedVal);
    }
}

void MzImage::writeLoadModule(const std::string &path) const {
    ofstream file{path, ios::binary};
    file.write(reinterpret_cast<const char*>(loadModuleData()), loadModuleSize());
}