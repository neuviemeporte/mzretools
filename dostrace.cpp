#include <iostream>
#include <string>
#include <cassert>
#include <regex>
#include <sstream>
#include <vector>
#include <memory>
#include <sys/stat.h>

#include "cpu.h"
#include "memory.h"
#include "syscall.h"
#include "error.h"
#include "util.h"

using namespace std;

static const string VERSION = "0.1";

struct VM {
    unique_ptr<Cpu> cpu;
    unique_ptr<Arena> memory;
    unique_ptr<Dos> os;
};

enum CmdStatus {
    CMD_OK, CMD_FAIL, CMD_UNKNOWN, CMD_EXIT
};

void printHelp() {
    cout << "Supported commands:" << endl
        << "load <executable_path> [address] - load DOS executable at specified address, \n"
            "\tif missing then current start of free memory will be used\n"
            "\tAddress format: <segment>:<offset>, e.g. 0ABC:0123, must be within conventional memory range" << endl
        << "exit - finish session" << endl;
}

CmdStatus execCommand(const string &cmd) {
    if (cmd.empty()) return CMD_OK;

    // split command into tokens at whitespace
    // TODO: support spaces and quotes inside commands
    auto iss = istringstream{cmd};
    vector<string> cmdTokens;
    string token;
    while (iss >> token) {
        cmdTokens.push_back(token);
    }
    assert(!cmdTokens.empty());
    const string verb = cmdTokens.front();
    const size_t paramCount = cmdTokens.size() - 1;

    if (verb == "exit" && paramCount == 0) return CMD_EXIT;
    else if (verb == "load") {
        if (paramCount < 1) {
            cout << "load command needs at least 1 parameter" << endl;
            return CMD_FAIL;
        }
        const string exefile = cmdTokens[1];
        struct stat statbuf;
        int error = stat(exefile.c_str(), &statbuf);
        if (error) {
            cout << "Executable file '" << exefile << "' does not exist!" << endl;
            return CMD_FAIL;
        }
        else if (statbuf.st_size == 0) {
            cout << "Executable file '" << exefile << "' has size zero!" << endl;
            return CMD_FAIL;
        }
    }
    // TODO: implement verb exec
    // TODO: implement verb script
    else if (verb == "help") {
        printHelp();
        return CMD_OK;
    }
    else return CMD_UNKNOWN;
}

int main(int argc, char* argv[])
{
    // initialize virtual machine components for emulation
    VM vm;
    vm.cpu = make_unique<Cpu_8086>();
    vm.memory = make_unique<Arena>();
    vm.os = make_unique<Dos>(vm.cpu.get(), vm.memory.get());

    string command;
    int status = 0;
    cout << "This is dostrace v" << VERSION << endl;
    while (true) {
        cout << "> ";
        // TODO: implement command history
        getline(cin, command);
        try {
            const CmdStatus status = execCommand(command);
            switch (status) {
            case CMD_UNKNOWN:
                cout << "Unrecognized command: " << command << endl;
                break;
            case CMD_EXIT:
                return 0;
            default:
                break;
            }
        }
        catch (Error &e) {
            cout << "Error: " << e.what() << endl;
            status = 1;
            break;
        }
    }

    return status;
}
