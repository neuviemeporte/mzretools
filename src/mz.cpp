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

MzImage::MzImage(const std::string &path) : path_(path), loadModuleData_(nullptr) {
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

    // read in any bytes between end of header and beginning of relocation table: optional overlay information?
    output("reloc @"s + hexVal(header_.reloc_table_offset) + ", hdr size = " + hexVal(HEADER_SIZE), LOG_OS);
    if (header_.reloc_table_offset > HEADER_SIZE) {
        const Size ovlInfoSize = header_.reloc_table_offset - HEADER_SIZE;
        ovlinfo_ = vector<Byte>(ovlInfoSize);
        fread(ovlinfo_.data(), 1, ovlInfoSize, mzFile);
    }

    // read in relocation entries
    if (header_.num_relocs) {
        if (fseek(mzFile, header_.reloc_table_offset, SEEK_SET) != 0) {
            fclose(mzFile);
            throw IoError("Unable to seek to relocation table!");
        }
        for (size_t i = 0; i < header_.num_relocs; ++i) {
            Word relocData[2];
            auto relocReadSize = fread(&relocData, 1, RELOC_SIZE, mzFile);
            if (relocReadSize != RELOC_SIZE) {
                fclose(mzFile);
                throw IoError("Invalid relocation read size from MzImage file: "s + to_string(relocReadSize));
            }
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
        reloc.value = relocVal;
    }
    fclose(mzFile);
}

std::string MzImage::dump() const {
    ostringstream msg;
    char signatureStr[sizeof(Word) + 1] = {0};
    memcpy(signatureStr, &header_.signature, sizeof(Word));
    msg << "--- " << path_ << " MZ header (" << std::dec << HEADER_SIZE << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, signature) << "] signature = 0x" << header_.signature << " ('" << signatureStr << "')" << endl
        << "\t[0x" << hex << offsetof(Header, last_page_size) << "] last_page_size = " << std::hex << "0x" << header_.last_page_size
            << " (" << std::dec << header_.last_page_size << " bytes)" <<  endl       
        << "\t[0x" << hex << offsetof(Header, pages_in_file) << "] pages_in_file = " << std::dec << header_.pages_in_file 
            << " (" << header_.pages_in_file * PAGE_SIZE << " bytes)" << endl
        << "\t[0x" << hex << offsetof(Header, num_relocs) << "] num_relocs = " << std::dec << header_.num_relocs << endl
        << "\t[0x" << hex << offsetof(Header, header_paragraphs) << "] header_paragraphs = " << std::dec << header_.header_paragraphs 
            << " (" << header_.header_paragraphs * PARAGRAPH_SIZE << " bytes)" << endl
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

void MzImage::load(const Word loadSegment) {
    ifstream mzFile(path_, ios::binary);
    if (!mzFile.is_open()) throw IoError("Unable to open exe file: " + path_);
    mzFile.seekg(loadModuleOffset_);
    assert(loadModuleData_ == nullptr);
    loadModuleData_ = new Byte[loadModuleSize_];
    mzFile.read(reinterpret_cast<char*>(loadModuleData_), loadModuleSize_);
    const auto bytesRead = mzFile.gcount();
    if (!mzFile) throw IoError("Error while reading load module data from "s + path_);
    if (bytesRead != loadModuleSize_) throw IoError("Incorrect number of bytes read from "s  + path_ + ": " + to_string(bytesRead));
    mzFile.close();
    // patch relocations
    for (const auto &r : relocs_) {
        const Address addr(r.segment, r.offset);
        const Offset off = addr.toLinear();
        const Word patchedVal = r.value + loadSegment;
        loadModuleData_[off] = patchedVal;
    }
}
