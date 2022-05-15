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
#include "error.h"
#include "util.h"

using namespace std;

VM::VM() {
    memory = make_unique<Arena>(); 
    cpu = make_unique<Cpu_8086>();
    os = make_unique<Dos>(cpu.get(), memory.get());
    // cpu needs r/w access to memory data and to the OS to call the interrupt handler
    cpu->setMemBase(memory->base());
    cpu->setOs(os.get());
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
        << "cpu - print CPU info" << endl
        << "exec - start executing code at current cpu instruction pointer" << endl
        << "exit - finish session" << endl;
}

void cmdOutput(const string &msg) {
    cout << msg << endl;
}

void cmdError(const string &verb, const string &message) {
    cout << "Error running command '" << verb << "': " << message << endl;
}

CmdStatus loadCommand(VM &vm, const vector<string> &params) {
    if (params.empty()) {
        cmdOutput("load command needs at least 1 parameter");
        return CMD_FAIL;
    }
    const string &path = params[0];
    // check if file exists
    const auto file = checkFile(path);
    if (!file.exists) {
        cmdOutput("Executable file does not exist!");
        return CMD_FAIL;
    }
    // make sure it's not empty
    if (file.size == 0) {
        cmdOutput("Executable file has size zero!");
        return CMD_FAIL;
    }
    // look for MZ signature
    ifstream data{path, ios::binary};
    if (!data.is_open()) {
        cmdOutput("Unable to open executable file '"s + path + "' for reading!");
        return CMD_FAIL;
    }
    char magic[2], mz[2] = { 'M', 'Z' };
    if (!data.read(&magic[0], sizeof(magic))) {
        cmdOutput("Unable to read magic from file!");
        return CMD_FAIL;
    }
    data.close();
    const Size memFree = vm.memory->available();
    // load as exe
    if (memcmp(magic, mz, 2) == 0) {
        cmdOutput("Detected MZ header, loading as .exe image");
        MzImage image{path};
        if (image.loadModuleSize() > memFree) {
            cmdOutput("Load module size ("s + to_string(image.loadModuleSize()) + " of executable exceeds available memory: " + to_string(memFree));
            return CMD_FAIL;
        }
        vm.os->loadExe(image);
    }
    // load as com
    else {
        cmdOutput("No MZ header detected, loading as .com image");
        if (file.size > MAX_COMFILE_SIZE) {
            cmdOutput("File size ("s + to_string(file.size) + ") exceeds .com file size limit: " + to_string(MAX_COMFILE_SIZE)); 
            return CMD_FAIL;
        }
        if (file.size > memFree) {
            cmdOutput("File size (" + to_string(file.size) + ") exceeds available memory: " + to_string(memFree));
            return CMD_FAIL;
        }
        throw ImplementError();
    }
    return CMD_OK;
}

CmdStatus execCommand(VM &vm) {
    vm.cpu->exec();
    return CMD_OK;
}

// TODO: support address ranges for start and end
CmdStatus dumpCommand(VM &vm, const vector<string> &params) {
    if (params.empty()) {
        cmdOutput("dump command needs at least 1 parameter!");
        return CMD_FAIL;
    }
    const string &path = params[0];
    const Offset start = 0;
    // check if file exists
    const auto file = checkFile(path);
    if (file.exists) {
        // TODO: ask for overwrite
        cmdOutput("Dump file '"s + path + "' already exists, overwriting!");
        //return CMD_FAIL;
    }    
    vm.memory->dump(path);
    return CMD_OK;
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
            cmdError(verb, "trailing arugments");
            return CMD_FAIL;
        }
        return CMD_EXIT;
    }
    else if (verb == "load") {
        if (paramCount != 1) {
            cmdError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return loadCommand(vm, params);
    }
    else if (verb == "dump") {
        if (paramCount != 1) {
            cmdError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return dumpCommand(vm, params);
    }
    else if (verb == "cpu") {
        if (paramCount != 0) {
            cmdError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        cmdOutput(vm.cpu->info());
    }
    else if (verb == "exec") {
        if (paramCount != 0) {
            cmdError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return execCommand(vm);
    }
    // TODO: implement verb script
    else if (verb == "help") {
        printHelp();
        return CMD_OK;
    }
    else return CMD_UNKNOWN;
}


