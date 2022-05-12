#include <sstream>
#include <iostream>
#include <iomanip>

#include "cpu.h"
#include "util.h"

using namespace std;

void cpuMessage(const string &msg) {
    cout << msg << endl;
}

std::string Cpu_8086::info() const {
    ostringstream str;
    str << std::hex << "IP = " << regs_.IP << ", FLAGS = " << regs_.FLAGS << " / " << binString(regs_.FLAGS) << endl
        << "AX = " << regs_.AX << ", BX = " << regs_.BX << ", CX = " << regs_.CX << ", DX = " << regs_.DX << endl
        << "SI = " << regs_.SI << ", DI = " << regs_.DI << ", BP = " << regs_.BP << ", SP = " << regs_.SP << endl
        << "CS = " << regs_.CS << ", DS = " << regs_.DS << ", SS = " << regs_.SS << ", ES = " << regs_.ES;
    return str.str();
}

void Cpu_8086::reset(const SegmentedAddress &code, const SegmentedAddress &stack, const Word &pspSeg) {
    done_ = false;
    regs_ = Regs();
    regs_.CS = code.segment;
    regs_.IP = code.offset;
    regs_.SS = stack.segment;
    regs_.SP = stack.offset;
    regs_.DS = regs_.ES = pspSeg;
}

void Cpu_8086::exec() {
    SegmentedAddress startAddr(regs_.CS, regs_.IP);
    cpuMessage("Starting execution at address "s + startAddr.toString() + " / " + hexVal(startAddr.toLinear()));
    Byte* code = memBase_ + startAddr.toLinear();
    while (!done_) {
        
    }
}