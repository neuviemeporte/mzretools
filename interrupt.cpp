#include <string>
#include <iostream>

#include "interrupt.h"
#include "cpu.h"
#include "dos.h"
#include "error.h"
#include "util.h"


using namespace std;

enum Interrupt : Byte {
    INT_DOS_TERMINATE = 0x20,
    INT_DOS = 0x21,
};

void intMessage(const string &msg) {
    cout << msg << endl;
}

IntStatus InterruptHandler::interrupt(Cpu &cpu, const Byte num) {
    const Byte 
        funcHi = cpu.reg8(REG_AH), 
        funcLo = cpu.reg8(REG_AL);
    switch (num) {
    case INT_DOS_TERMINATE:
        return INT_TERMINATE;
    case INT_DOS:
        dosFunction(cpu, funcHi, funcLo);
        return INT_OK;
    default:
        throw InterruptError("Interrupt not implemented: "s + hexVal(num) + " at " + cpu.csip().toString());
    }
}

enum DosFunction : Byte {
    DOS_VERSION = 0x30,
};

void InterruptHandler::dosFunction(Cpu &cpu, const Byte funcHi, const Byte funcLo) {
    switch (funcHi)
    {
    case DOS_VERSION:
        cpu.reg8(REG_AL) = static_cast<Byte>(dos_->version());
        cpu.reg8(REG_AH) = 0;
        break;
    default:
        throw InterruptError("DOS interrupt function not implemented: "s + hexVal(funcHi) + "/" + hexVal(funcLo));
    }
}