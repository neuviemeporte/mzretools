#include <iostream>
#include <string>
#include <cassert>
#include <regex>
#include <sstream>
#include <fstream>
#include <vector>
#include <memory>
#include <regex>

#include "dos/system.h"
#include "dos/cpu.h"
#include "dos/memory.h"
#include "dos/interrupt.h"
#include "dos/mz.h"
#include "dos/error.h"
#include "dos/util.h"

using namespace std;

System::System() {
    mem_ = make_unique<Memory>(); 
    os_ = make_unique<Dos>(mem_.get());
    int_ = make_unique<InterruptHandler>(os_.get());
    cpu_ = make_unique<Cpu_8086>(mem_.get(), int_.get());
    cout << "Initialized VM, " << info() << endl;
}

string System::info() const {
    ostringstream infoStr;
    infoStr << "CPU: " << cpu_->type() << ", Memory: " << mem_->info() << ", OS: " << os_->name();
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
        if (paramCount > 1) {
            error(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return commandRun(params);
    }
    else if (verb == "analyze") {
        if (paramCount != 0) {
            error(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        return commandAnalyze();        
    }
    // TODO: implement verb script
    else if (verb == "help") {
        printHelp();
        return CMD_OK;
    }
    return CMD_UNKNOWN;
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
    const Size memFree= mem_->availableBytes();
    // load as exe
    if (memcmp(magic, mz, 2) == 0) {
        output("Detected MZ header, loading as .exe image");
        MzImage image{path};
        if (image.loadModuleSize() > memFree) {
            output("Load module size ("s + to_string(image.loadModuleSize()) + " of executable exceeds available memory: " + to_string(memFree));
            return CMD_FAIL;
        }
        const auto loadModule = os_->loadExe(image);
        cpu_->init(loadModule.code, loadModule.stack, loadModule.size);
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

static const regex FARADDR_RE{"([0-9a-fA-F]{1,4}):([0-9a-fA-F]{1,4})"};

CmdStatus System::commandRun(const Params &params) {
    if (params.size() == 1) {
        const string entrypointStr = params.front();
        smatch match;
        if (regex_match(entrypointStr, match, FARADDR_RE)) {
            const string segStr = match.str(1), offStr = match.str(2);
            const Word 
                segment = stoi(segStr, nullptr, 16),
                offset  = stoi(offStr, nullptr, 16);
            const Address entrypoint(segment, offset);
            cpu_->init(entrypoint, Address{entrypoint.segment, 0xfffe}, 0);
        }
        else {
            output("Invalid entrypoint argument: " + entrypointStr);
            return CMD_FAIL;
        }
    }

    cpu_->run();
    return CMD_OK;
}

CmdStatus System::commandAnalyze() {
    analysis_ = cpu_->analyze();
    if (analysis_.success) return CMD_OK;
    else return CMD_FAIL;
}

// TODO: support address ranges for start and end
CmdStatus System::commandDump(const Params &params) {
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
    mem_->dump(path);
    return CMD_OK;
}