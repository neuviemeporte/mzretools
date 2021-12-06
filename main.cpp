#include <iostream>
#include "dostrace.h"
#include "error.h"

using namespace std;

static const string VERSION = "0.1";

int main(int argc, char* argv[])
{
    // initialize virtual machine for emulation
    VM vm;
    // enter interactive mode, display prompt and keep parsing commands
    string command;
    int status = 0;
    cout << "This is dostrace v" << VERSION << endl << "Type 'help' for a list of commands" << endl;
    while (true) {
        cout << "> ";
        // TODO: implement command history
        getline(cin, command);
        try {
            const CmdStatus status = execCommand(vm, command);
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