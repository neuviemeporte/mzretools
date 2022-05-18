#include <iostream>
#include <string>
#include <cassert>
#include <regex>
#include <sstream>
#include <fstream>
#include <vector>
#include <memory>

#include "system.h"
#include "cpu.h"
#include "memory.h"
#include "mz.h"
#include "error.h"
#include "util.h"

using namespace std;

System::System() {
    memory_ = make_unique<Arena>(); 
    os_ = make_unique<Dos>(memory_.get());
    int_ = make_unique<InterruptHandler>(os_.get());
    cpu_ = make_unique<Cpu_8086>(memory_->base(), int_.get());
    cout << "Initialized VM, " << info() << endl;
}

string System::info() const {
    ostringstream infoStr;
    infoStr << "CPU: " << cpu_->type() << ", Memory: " << memory_->info() << ", OS: " << os_->name();
    return infoStr.str();
}

CmdStatus System::commandDispatch(const string &cmd) {
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
        return loadCommand(params);
    }
    else if (verb == "dump") {
        if (paramCount != 1) {
            cmdError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return dumpCommand(params);
    }
    else if (verb == "cpu") {
        if (paramCount != 0) {
            cmdError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        cmdOutput(cpu_->info());
    }
    else if (verb == "exec") {
        if (paramCount != 0) {
            cmdError(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return execCommand();
    }
    // TODO: implement verb script
    else if (verb == "help") {
        printHelp();
        return CMD_OK;
    }
    else return CMD_UNKNOWN;
}


void System::printHelp() {
    cout << "Supported commands:" << endl
        << "load <executable_path> - load DOS executable at start of available memory" << endl
        << "dump <file_path> - dump contents of emulated memory to file" << endl
        << "cpu - print CPU info" << endl
        << "exec - start executing code at current cpu instruction pointer" << endl
        << "exit - finish session" << endl;
}

void System::cmdOutput(const string &msg) {
    cout << msg << endl;
}

void System::cmdError(const string &verb, const string &message) {
    cout << "Error running command '" << verb << "': " << message << endl;
}

CmdStatus System::loadCommand(const vector<string> &params) {
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
    const Size memFree = memory_->available();
    // load as exe
    if (memcmp(magic, mz, 2) == 0) {
        cmdOutput("Detected MZ header, loading as .exe image");
        MzImage image{path};
        if (image.loadModuleSize() > memFree) {
            cmdOutput("Load module size ("s + to_string(image.loadModuleSize()) + " of executable exceeds available memory: " + to_string(memFree));
            return CMD_FAIL;
        }
        os_->loadExe(image, loadedCode_, loadedStack_);
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
        throw SystemError("COM file support not yet implemented"s);
    }
    return CMD_OK;
}

CmdStatus System::execCommand() {
    cmdOutput("Starting execution at entrypoint "s + loadedCode_.toString() + " / " + hexVal(loadedCode_.toLinear()));
    cpu_->execute(loadedCode_, loadedStack_);
    return CMD_OK;
}

// TODO: support address ranges for start and end
CmdStatus System::dumpCommand(const vector<string> &params) {
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
    memory_->dump(path);
    return CMD_OK;
}