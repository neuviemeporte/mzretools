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

CmdStatus System::command(const string &cmd) {
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
            error(verb, "trailing arugments");
            return CMD_FAIL;
        }
        return CMD_EXIT;
    }
    else if (verb == "load") {
        if (paramCount != 1) {
            error(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return commandLoad(params);
    }
    else if (verb == "dump") {
        if (paramCount != 1) {
            error(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return commandDump(params);
    }
    else if (verb == "cpu") {
        if (paramCount != 0) {
            error(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        output(cpu_->info());
    }
    else if (verb == "run") {
        if (paramCount != 0) {
            error(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return commandRun();
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

void System::output(const string &msg) {
    cout << msg << endl;
}

void System::error(const string &verb, const string &message) {
    cout << "Error running command '" << verb << "': " << message << endl;
}

CmdStatus System::commandLoad(const vector<string> &params) {
    if (params.empty()) {
        output("load command needs at least 1 parameter");
        return CMD_FAIL;
    }
    const string &path = params[0];
    // check if file exists
    const auto file = checkFile(path);
    if (!file.exists) {
        output("Executable file does not exist!");
        return CMD_FAIL;
    }
    // make sure it's not empty
    if (file.size == 0) {
        output("Executable file has size zero!");
        return CMD_FAIL;
    }
    // look for MZ signature
    ifstream data{path, ios::binary};
    if (!data.is_open()) {
        output("Unable to open executable file '"s + path + "' for reading!");
        return CMD_FAIL;
    }
    char magic[2], mz[2] = { 'M', 'Z' };
    if (!data.read(&magic[0], sizeof(magic))) {
        output("Unable to read magic from file!");
        return CMD_FAIL;
    }
    data.close();
    const Size memFree= memory_->availableBytes();
    // load as exe
    if (memcmp(magic, mz, 2) == 0) {
        output("Detected MZ header, loading as .exe image");
        MzImage image{path};
        if (image.loadModuleSize() > memFree) {
            output("Load module size ("s + to_string(image.loadModuleSize()) + " of executable exceeds available memory: " + to_string(memFree));
            return CMD_FAIL;
        }
        os_->loadExe(image, loadedCode_, loadedStack_);
    }
    // load as com
    else {
        output("No MZ header detected, loading as .com image");
        if (file.size > MAX_COMFILE_SIZE) {
            output("File size ("s + to_string(file.size) + ") exceeds .com file size limit: " + to_string(MAX_COMFILE_SIZE)); 
            return CMD_FAIL;
        }
        if (file.size > memFree) {
            output("File size (" + to_string(file.size) + ") exceeds available memory: " + to_string(memFree));
            return CMD_FAIL;
        }
        throw SystemError("COM file support not yet implemented"s);
    }
    return CMD_OK;
}

CmdStatus System::commandRun() {
    output("Starting execution at entrypoint "s + loadedCode_.toString() + " / " + hexVal(loadedCode_.toLinear()));
    cpu_->init(loadedCode_, loadedStack_);
    cpu_->run();
    return CMD_OK;
}

// TODO: support address ranges for start and end
CmdStatus System::commandDump(const vector<string> &params) {
    if (params.empty()) {
        output("dump command needs at least 1 parameter!");
        return CMD_FAIL;
    }
    const string &path = params[0];
    const Offset start = 0;
    // check if file exists
    const auto file = checkFile(path);
    if (file.exists) {
        // TODO: ask for overwrite
        output("Dump file '"s + path + "' already exists, overwriting!");
        //return CMD_FAIL;
    }    
    memory_->dump(path);
    return CMD_OK;
}