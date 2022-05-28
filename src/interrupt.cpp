#include <string>
#include <iostream>

#include "dos/interrupt.h"
#include "dos/memory.h"
#include "dos/cpu.h"
#include "dos/dos.h"
#include "dos/error.h"
#include "dos/util.h"


using namespace std;

enum Interrupt : Byte {
    INT_DOS_TERMINATE = 0x20,
    INT_DOS = 0x21,
};

void intMessage(const string &msg) {
    cout << msg << endl;
}

IntStatus InterruptHandler::interrupt(const Byte num, Registers &regs) {
    const Byte 
        funcHi = regs.bit8(REG_AH), 
        funcLo = regs.bit8(REG_AL);
    switch (num) {
    case INT_DOS_TERMINATE:
        return INT_TERMINATE;
    case INT_DOS:
        dosFunction(funcHi, funcLo, regs);
        return INT_OK;
    default:
        throw InterruptError("Interrupt not implemented: "s + hexVal(num) + " at " + regs.csip().toString());
    }
}

enum DosFunction : Byte {
    DOS_VERSION = 0x30,
};

void InterruptHandler::dosFunction(const Byte funcHi, const Byte funcLo, Registers &regs) {
    switch (funcHi)
    {
    case DOS_VERSION:
        regs.bit8(REG_AL) = static_cast<Byte>(dos_->version());
        regs.bit8(REG_AH) = 0;
        break;
    default:
        throw InterruptError("DOS interrupt function not implemented: "s + hexVal(funcHi) + "/" + hexVal(funcLo));
    }
}