#include <iostream>
#include "dos/system.h"
#include "dos/error.h"

// TODO:
// - reset command
// - view memory command
// - load raw binary data at offset
// - backtrace

using namespace std;

static const string VERSION = "0.1";

int main(int argc, char* argv[])
{
    // TODO: implement batch mode, execute commands from script and exit
    cout << "This is dostrace v" << VERSION << endl;
    // initialize virtual machine for emulation
    System vm;
    // enter interactive mode, display prompt and keep parsing commands
    cout << "Type 'help' for a list of commands" << endl;
    string command;
    int status = 0;
    while (true) {
        cout << "> ";
        // TODO: implement command history
        getline(cin, command);
        try {
            const CmdStatus status = vm.command(command);
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
            if (e.fatal()) {
                status = 1;
                break;
            } 
        }
    }

    return status;
}