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

inline Address InterruptHandler::csip() const { 
    return {reg16(REG_CS), reg16(REG_IP)}; 
}

IntStatus InterruptHandler::interrupt(const Byte num) {
    const Byte 
        funcHi = reg8(REG_AH), 
        funcLo = reg8(REG_AL);
    switch (num) {
    case INT_DOS_TERMINATE:
        return INT_TERMINATE;
    case INT_DOS:
        dosFunction(funcHi, funcLo);
        return INT_OK;
    default:
        throw InterruptError("Interrupt not implemented: "s + hexVal(num) + " at " + csip().toString());
    }
}

enum DosFunction : Byte {
    DOS_VERSION = 0x30,
};

void InterruptHandler::dosFunction(const Byte funcHi, const Byte funcLo) {
    switch (funcHi)
    {
    case DOS_VERSION:
        reg8(REG_AL) = static_cast<Byte>(dos_->version());
        reg8(REG_AH) = 0;
        break;
    default:
        throw InterruptError("DOS interrupt function not implemented: "s + hexVal(funcHi) + "/" + hexVal(funcLo));
    }
}