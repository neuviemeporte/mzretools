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
#include "dos/output.h"

using namespace std;

System::System() {
    mem_ = make_unique<Memory>(); 
    os_ = make_unique<Dos>(mem_.get());
    int_ = make_unique<InterruptHandler>(os_.get());
    cpu_ = make_unique<Cpu_8086>(mem_.get(), int_.get());
    info("Initialized VM, "s + systemInfo());
}

string System::systemInfo() const {
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
    if (verb == "exit" || verb == "quit") {
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
        return commandDump(params);
    }
    else if (verb == "cpu") {
        if (paramCount != 0) {
            error(verb, "unexpected syntax");
            return CMD_FAIL;
        }
        info(cpu_->info());
        return CMD_OK;
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
    info("Supported commands:\n "s
        + "load <executable_path> - load DOS executable at start of available memory\n"
        + "dump [from_address|from_offset] [to_address|to_offset|+size] [output_file] - output contents of emulated memory\n"
        + "cpu - print CPU info\n"
        + "run [address] - start executing code at current cpu instruction pointer\n"
        + "analyze - analyze executable, try to find subroutines\n"
        + "exit - finish session\n");
}

void System::info(const string &msg) {
    output(msg, LOG_SYSTEM, LOG_INFO);
}

void System::error(const string &verb, const string &message) {
    output("Error running command '"s + verb + "': " + message, LOG_SYSTEM, LOG_ERROR);
}

CmdStatus System::commandLoad(const vector<string> &params) {
    if (params.empty()) {
        info("load command needs at least 1 parameter");
        return CMD_FAIL;
    }
    const string &path = params[0];
    // check if file exists
    const auto file = checkFile(path);
    if (!file.exists) {
        info("Executable file does not exist!");
        return CMD_FAIL;
    }
    // make sure it's not empty
    if (file.size == 0) {
        info("Executable file has size zero!");
        return CMD_FAIL;
    }
    // look for MZ signature
    ifstream data{path, ios::binary};
    if (!data.is_open()) {
        info("Unable to open executable file '"s + path + "' for reading!");
        return CMD_FAIL;
    }
    char magic[2], mz[2] = { 'M', 'Z' };
    if (!data.read(&magic[0], sizeof(magic))) {
        info("Unable to read magic from file!");
        return CMD_FAIL;
    }
    data.close();
    const Size memFree= mem_->availableBytes();
    // load as exe
    if (memcmp(magic, mz, 2) == 0) {
        info("Detected MZ header, loading as .exe image");
        MzImage image{path};
        if (image.loadModuleSize() > memFree) {
            info("Load module size ("s + to_string(image.loadModuleSize()) + " of executable exceeds available memory: " + to_string(memFree));
            return CMD_FAIL;
        }
        const auto loadModule = os_->loadExe(image);
        cpu_->init(loadModule.code, loadModule.stack, loadModule.size);
    }
    // load as com
    else {
        info("No MZ header detected, loading as .com image");
        if (file.size > MAX_COMFILE_SIZE) {
            info("File size ("s + to_string(file.size) + ") exceeds .com file size limit: " + to_string(MAX_COMFILE_SIZE)); 
            return CMD_FAIL;
        }
        if (file.size > memFree) {
            info("File size (" + to_string(file.size) + ") exceeds available memory: " + to_string(memFree));
            return CMD_FAIL;
        }
        throw SystemError("COM file support not yet implemented"s);
    }
    return CMD_OK;
}

// TODO: optinally specify offset to run from
CmdStatus System::commandRun(const Params &params) {
    const size_t paramCount = params.size();
    if (paramCount > 1) {
        info("Invalid argument count");
        return CMD_FAIL;
    }
        
    try {
        if (paramCount == 1) { // optional entrypoint argument
            const string &entrypointStr = params.front();
            const Address entrypoint(entrypointStr);
            cpu_->init(entrypoint, Address{entrypoint.segment, 0xfffe}, 0);
        } 
        cpu_->run();
    }
    catch (ArgError &e) {
        info(e.why());
        return CMD_FAIL;
    }

    return CMD_OK;
}

CmdStatus System::commandAnalyze() {
    analysis_ = cpu_->analyze();
    if (analysis_.success) return CMD_OK;
    else return CMD_FAIL;
}

// TODO: support address ranges for start and end
CmdStatus System::commandDump(const Params &params) {
    const size_t paramCount = params.size();
    Block range{0, mem_->size() - 1}; // full range by default
    std::string path;

    try {
        if (paramCount == 1) { // dump entire memory to file
            path = params[0];
        }
        else if (paramCount == 2) { // dump range to screen
            range = Block{params[0], params[1]};
        }
        else if (paramCount == 3) { // dump range to file
            range = Block{params[0], params[1]};
            path = params[2];
        }
        else if (paramCount != 0) { // dump entire memory to screen
            error("dump", "Too many arguments");
            return CMD_FAIL;
        }
    }
    catch (ArgError &e) {
        error("dump", e.why());
        return CMD_FAIL;
    }


    if (paramCount == 1 || paramCount == 3) { // file output
        info("Dumping memory in range "s + range.toString() + " to file: " + path);
        // check if file exists
        const auto file = checkFile(path);
        if (file.exists) {
            // TODO: ask for overwrite
            info("Dump file '"s + path + "' already exists, overwriting!");
        }                
        mem_->dump(range, path);
    }
    else { // screen output
        info("Dumping memory in range "s + range.toString());
        hexDump(mem_->pointer(range.begin), range.size(), range.begin.toLinear(), false);
    }

    return CMD_OK;
}