#include <iostream>
#include <string>
#include <cassert>
#include <regex>
#include <sstream>
#include <fstream>
#include <vector>
#include <memory>
#include <sys/stat.h>

#include "dostrace.h"
#include "cpu.h"
#include "memory.h"
#include "mz.h"
#include "syscall.h"
#include "error.h"
#include "util.h"

using namespace std;

VM::VM() : 
    cpu(make_unique<Cpu_8086>()), 
    memory(make_unique<Arena>()), 
    os(make_unique<Dos>(cpu.get(), memory.get())) {}

void printHelp() {
    cout << "Supported commands:" << endl
        << "load <executable_path> - load DOS executable at start of available memory" << endl
        << "dump <file_path> - dump contents of emulated memory to file"
        << "exit - finish session" << endl;
}

CmdStatus loadCommand(VM &vm, const vector<string> &params) {
    if (params.empty()) {
        cout << "load command needs at least 1 parameter" << endl;
        return CMD_FAIL;
    }
    const string &path = params[0];
    // check if file exists
    struct stat statbuf;
    int error = stat(path.c_str(), &statbuf);
    if (error) {
        cout << "Executable file does not exist!" << endl;
        return CMD_FAIL;
    }
    // make sure it's not empty
    const Size fileSize = statbuf.st_size;
    if (fileSize == 0) {
        cout << "Executable file has size zero!" << endl;
        return CMD_FAIL;
    }
    // look for MZ signature
    ifstream file{path, ios::binary};
    if (!file.is_open()) {
        cout << "Unable to open executable file '" << path << "' for reading!" << endl;
        return CMD_FAIL;
    }
    char magic[2], mz[2] = { 'M', 'Z' };
    if (!file.read(&magic[0], sizeof(magic))) {
        cout << "Unable to read magic from file!" << endl;
        return CMD_FAIL;
    }
    file.close();
    const Size memFree = vm.memory->free();
    // load as exe
    if (memcmp(magic, mz, 2) == 0) {
        cout << "Detected MZ header, loading as .exe image" << endl;
        MzImage image{path};
        if (image.loadModuleSize() > memFree) {
            cout << "Load module size (" << image.loadModuleSize() << " of executable exceeds available memory: " << memFree << endl;
            return CMD_FAIL;
        }
        image.loadToArena(*vm.memory);
    }
    // load as com
    else {
        cout << "No MZ header detected, loading as .com image" << endl;
        if (fileSize > MAX_COMFILE_SIZE) {
            cout << "File size (" << fileSize << ") exceeds .com file size limit: " << MAX_COMFILE_SIZE << endl; 
            return CMD_FAIL;
        }
        if (fileSize > memFree) {
            cout << "File size (" << fileSize << ") exceeds available memory: " << memFree << endl;
            return CMD_FAIL;
        }
        throw ImplementError();
    }
    return CMD_OK;
}

// TODO: support address ranges for start and end
CmdStatus dumpCommand(VM &vm, const vector<string> &params) {
    if (params.empty()) {
        cout << "dump command needs at least 1 parameter!" << endl;
        return CMD_FAIL;
    }
    const string &path = params[0];
    const Offset start = 0;
    // check if file exists
    struct stat statbuf;
    int error = stat(path.c_str(), &statbuf);
    if (error == 0) {
        // TODO: ask for overwrite
        cout << "Dump file already exists!" << endl;
        return CMD_FAIL;
    }    
    // open file for writing
    ofstream file(path, ios::binary);
    if (!file.is_open()) {
        cout << "Unable to open file '" << path << "' for writing!" << endl;
        return CMD_FAIL;
    }
    // dump arena contents to file in page-sized chunks
    assert(MEM_TOTAL % PAGE == 0);
    const char *memPtr = reinterpret_cast<const char*>(vm.memory->pointer(start));
    for (Size s = 0; s < MEM_TOTAL; s += PAGE) {
        file.write(memPtr, PAGE);
    }
    return CMD_OK;
}

CmdStatus execCommand(VM &vm, const string &cmd) {
    if (cmd.empty()) 
        return CMD_OK;

    // split command into tokens at whitespace
    // TODO: support spaces and quotes inside commands
    auto iss = istringstream{cmd};
    string token, verb;
    vector<string> params;
    size_t tokenCount = 0;
    while (iss >> token) {
        if (tokenCount == 0)
            verb = token;
        else
            params.push_back(token);
        tokenCount++;
    }
    assert(!verb.empty() && tokenCount > 0);
    const size_t paramCount = tokenCount - 1;
    if (verb == "exit" && paramCount == 0) 
        return CMD_EXIT;
    else if (verb == "load" && paramCount == 1) {
        return loadCommand(vm, params);
    }
    else if (verb == "dump" && paramCount == 1) {
        return dumpCommand(vm, params);
    }
    // TODO: implement verb exec
    // TODO: implement verb script
    else if (verb == "help") {
        printHelp();
        return CMD_OK;
    }
    else return CMD_UNKNOWN;
}


