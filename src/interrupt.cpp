#include <string>
#include <iostream>

#include "dos/interrupt.h"
#include "dos/memory.h"
#include "dos/cpu.h"
#include "dos/dos.h"
#include "dos/error.h"
#include "dos/util.h"
#include "dos/output.h"

using namespace std;

enum Interrupt : Byte {
    INT_DOS_TERMINATE = 0x20,
    INT_DOS = 0x21,
};

void intMessage(const string &msg) {
    output(msg, LOG_INTERRUPT, LOG_INFO);
}

IntStatus InterruptHandler::interrupt(const Byte num, Registers &regs) {
    const Byte 
        funcHi = regs.get(REG_AH), 
        funcLo = regs.get(REG_AL);
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
        regs.set(REG_AL, static_cast<Byte>(dos_->version()));
        regs.set(REG_AH, 0);
        break;
    default:
        throw InterruptError("DOS interrupt function not implemented: "s + hexVal(funcHi) + "/" + hexVal(funcLo));
    }
}