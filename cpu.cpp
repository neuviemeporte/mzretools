#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include "cpu.h"
#include "util.h"
#include "error.h"
#include "dos.h"

using namespace std;

void cpuMessage(const string &msg) {
    cout << msg << endl;
}

Cpu_8086::Cpu_8086() : Cpu(), done_(false) {
    setupRegs();
    cpuMessage("Initialized CPU, memory base: "s + hexVal(reinterpret_cast<long>(memBase_)));
}

std::string Cpu_8086::info() const {
    ostringstream str;
    str << std::hex << "IP = " << reg16(REG_IP) << ", FLAGS = " << reg16(REG_FLAGS) << " / " << binString(reg16(REG_FLAGS)) << endl
        << "AX = " << reg16(REG_AX) << ", BX = " << reg16(REG_BX) << ", CX = " << reg16(REG_CX) << ", DX = " << reg16(REG_DX) << endl
        << "SI = " << reg16(REG_SI) << ", DI = " << reg16(REG_DI) << ", BP = " << reg16(REG_BP) << ", SP = " << reg16(REG_SP) << endl
        << "CS = " << reg16(REG_CS) << ", DS = " << reg16(REG_DS) << ", SS = " << reg16(REG_SS) << ", ES = " << reg16(REG_ES);
    return str.str();
}

void Cpu_8086::reset(const SegmentedAddress &code, const SegmentedAddress &stack, const Word &pspSeg) {
    done_ = false;
    setupRegs();
    reg16(REG_CS) = code.segment;
    reg16(REG_IP) = code.offset;
    reg16(REG_SS) = stack.segment;
    reg16(REG_SP) = stack.offset;
    reg16(REG_DS) = reg16(REG_ES) = pspSeg;
}

enum Opcode : Byte {
    MOV_AL_Ib = 0xb0,
    MOV_CL_Ib = 0xb1,
    MOV_DL_Ib = 0xb2,
    MOV_BL_Ib = 0xb3,
    MOV_AH_Ib = 0xb4,
    MOV_CH_Ib = 0xb5,
    MOV_DH_Ib = 0xb6,
    MOV_BH_Ib = 0xb7,
    INT_Ib    = 0xcd,
};

void Cpu_8086::exec() {
    SegmentedAddress entrypoint(reg16(REG_CS), reg16(REG_IP)), codeSeg(reg16(REG_CS), 0);
    cpuMessage("Starting execution at entrypoint "s + entrypoint.toString() + " / " + hexVal(entrypoint.toLinear()));
    Byte* code = memBase_ + codeSeg.toLinear();
    Byte op, b;
    while (!done_) {
        op = code[reg16(REG_IP)];
        switch (op)
        {
        case MOV_AL_Ib:
        case MOV_CL_Ib:
        case MOV_DL_Ib:
        case MOV_BL_Ib:
        case MOV_AH_Ib:
        case MOV_CH_Ib:
        case MOV_DH_Ib:
        case MOV_BH_Ib:
            b = code[reg16(REG_IP) + 1];
            mov_byte(op & 0xf, b);
            reg16(REG_IP) += 2;
            break;
        case INT_Ib:
            b = code[reg16(REG_IP) + 1];
            reg16(REG_IP) += 2;            
            break;
        default:
            cpuMessage(info());
            throw CpuError("Unknown opcode at address "s + SegmentedAddress(reg16(REG_CS), reg16(REG_IP)).toString() + ": " + hexVal(op));
        }
    }
}

void Cpu_8086::setupRegs() {
    fill(begin(regs_.reg16), end(regs_.reg16), 0);
    setFlag(FLAG_IF, true);
    // these are supposed to be always 1 on 8086
    setFlag(FLAG_B12, true); 
    setFlag(FLAG_B13, true);
    setFlag(FLAG_B14, true); 
    setFlag(FLAG_B15, true);
}

void Cpu_8086::mov_byte(const Byte dst, const Byte val) {
    // destination register encoded in instruction opcode has different order than our register ids so conver it
    static RegId const id[] = { REG_AL, REG_CL, REG_DL, REG_BL, REG_AH, REG_CH, REG_DH, REG_BH } ;
    regs_.reg8[id[dst]] = val;
    setFlag(FLAG_ZF, val == 0);
}

void Cpu_8086::interrupt(const Byte val) {
    switch (val) {
        case 0x21:
            os_->interruptHandler();
            break;
        default:
            cpuMessage(info());
            throw CpuError("Unsupported interrupt at address "s + SegmentedAddress(reg16(REG_CS), reg16(REG_IP)).toString() + ": " + hexVal(val));
    }

}