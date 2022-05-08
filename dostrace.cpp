#include <iostream>
#include <string>
#include <cassert>
#include <regex>
#include <sstream>
#include <fstream>
#include <vector>
#include <memory>

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
    os(make_unique<Dos>(cpu.get(), memory.get())) {
    cout << "Initialized VM, " << info() << endl;
}

string VM::info() const {
    ostringstream infoStr;
    infoStr << "CPU: " << cpu->type() << ", Memory: " << memory->info() << ", OS: " << os->name();
    return infoStr.str();
}

void printHelp() {
    cout << "Supported commands:" << endl
        << "load <executable_path> - load DOS executable at start of available memory" << endl
        << "dump <file_path> - dump contents of emulated memory to file" << endl
        << "exit - finish session" << endl;
}

CmdStatus loadCommand(VM &vm, const vector<string> &params) {
    if (params.empty()) {
        cout << "load command needs at least 1 parameter" << endl;
        return CMD_FAIL;
    }
    const string &path = params[0];
    // check if file exists
    const auto file = checkFile(path);
    if (!file.exists) {
        cout << "Executable file does not exist!" << endl;
        return CMD_FAIL;
    }
    // make sure it's not empty
    if (file.size == 0) {
        cout << "Executable file has size zero!" << endl;
        return CMD_FAIL;
    }
    // look for MZ signature
    ifstream data{path, ios::binary};
    if (!data.is_open()) {
        cout << "Unable to open executable file '" << path << "' for reading!" << endl;
        return CMD_FAIL;
    }
    char magic[2], mz[2] = { 'M', 'Z' };
    if (!data.read(&magic[0], sizeof(magic))) {
        cout << "Unable to read magic from file!" << endl;
        return CMD_FAIL;
    }
    data.close();
    const Size memFree = vm.memory->available();
    // load as exe
    if (memcmp(magic, mz, 2) == 0) {
        cout << "Detected MZ header, loading as .exe image" << endl;
        MzImage image{path};
        if (image.loadModuleSize() > memFree) {
            cout << "Load module size (" << image.loadModuleSize() << " of executable exceeds available memory: " << memFree << endl;
            return CMD_FAIL;
        }
        vm.os->loadExe(image);
    }
    // load as com
    else {
        cout << "No MZ header detected, loading as .com image" << endl;
        if (file.size > MAX_COMFILE_SIZE) {
            cout << "File size (" << file.size << ") exceeds .com file size limit: " << MAX_COMFILE_SIZE << endl; 
            return CMD_FAIL;
        }
        if (file.size > memFree) {
            cout << "File size (" << file.size << ") exceeds available memory: " << memFree << endl;
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
    const auto file = checkFile(path);
    if (file.exists) {
        // TODO: ask for overwrite
        cout << "Dump file '" << path << "' already exists!" << endl;
        return CMD_FAIL;
    }    
    vm.memory->dump(path);
    return CMD_OK;
}

void commandError(const string &verb, const string &message) {
    cout << "Error running command '" << verb << "': " << message << endl;
}

CmdStatus commandDispatch(VM &vm, const string &cmd) {
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
    // todo: table of commands with arg counts and handlers
    if (verb == "exit") {
        if (paramCount != 0) {
            commandError(verb, "trailing arugments");
            return CMD_FAIL;
        }
        return CMD_EXIT;
    }
    else if (verb == "load") {
        if (paramCount != 1) {
            commandError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return loadCommand(vm, params);
    }
    else if (verb == "dump") {
        if (paramCount != 1) {
            commandError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
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


