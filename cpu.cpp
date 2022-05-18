#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>

#include "cpu.h"
#include "interrupt.h"
#include "util.h"
#include "error.h"

using namespace std;

void cpuMessage(const string &msg) {
    cout << msg << endl;
}

Cpu_8086::Cpu_8086(Byte *memBase, InterruptInterface *intif) : memBase_(memBase), int_(intif), done_(false) {
    resetRegs();
}

std::string Cpu_8086::info() const {
    ostringstream str;
    str << std::hex 
        << "IP = " << hexVal(reg16(REG_IP)) << ", FLAGS = " << hexVal(reg16(REG_FLAGS)) << " / " << binString(reg16(REG_FLAGS)) << endl
        << "AX = " << hexVal(reg16(REG_AX)) << ", BX = " << hexVal(reg16(REG_BX)) << ", CX = " << hexVal(reg16(REG_CX)) << ", DX = " << hexVal(reg16(REG_DX)) << endl
        << "SI = " << hexVal(reg16(REG_SI)) << ", DI = " << hexVal(reg16(REG_DI)) << ", BP = " << hexVal(reg16(REG_BP)) << ", SP = " << hexVal(reg16(REG_SP)) << endl
        << "CS = " << hexVal(reg16(REG_CS)) << ", DS = " << hexVal(reg16(REG_DS)) << ", SS = " << hexVal(reg16(REG_SS)) << ", ES = " << hexVal(reg16(REG_ES));
    return str.str();
}

enum Opcode : Byte {
    CMP_AL_Ib = 0x3c,
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

void Cpu_8086::execute(const SegmentedAddress &codeAddr, const SegmentedAddress &stackAddr) {
    // initialize registers
    resetRegs();
    reg16(REG_CS) = codeAddr.segment;
    reg16(REG_IP) = codeAddr.offset;
    reg16(REG_SS) = stackAddr.segment;
    reg16(REG_SP) = stackAddr.offset;
    SegmentedAddress codeSeg(reg16(REG_CS), 0);
    const Offset codeLinearAddr = codeSeg.toLinear();
    const Offset pspLinearAddr = codeLinearAddr - PSP_SIZE;
    SegmentedAddress pspAddr(pspLinearAddr);
    assert(pspAddr.offset == 0);
    reg16(REG_DS) = reg16(REG_ES) = pspAddr.segment;

    // obtain pointer to beginning of executable code
    Byte* code = memBase_ + codeLinearAddr;
    Byte op, argb;
    done_ = false;
    while (!done_) {
        op = code[reg16(REG_IP)];
        switch (op) {
        case CMP_AL_Ib:
            argb = code[reg16(REG_IP) + 1];
            
            reg16(REG_IP) += 2;
            break;
        case MOV_AL_Ib:
        case MOV_CL_Ib:
        case MOV_DL_Ib:
        case MOV_BL_Ib:
        case MOV_AH_Ib:
        case MOV_CH_Ib:
        case MOV_DH_Ib:
        case MOV_BH_Ib:
            argb = code[reg16(REG_IP) + 1];
            mov_byte(op & 0xf, argb);
            reg16(REG_IP) += 2;
            break;
        case INT_Ib:
            argb = code[reg16(REG_IP) + 1];
            // invoke interrupt handler
            int_->interrupt(*this, argb);
            reg16(REG_IP) += 2;
            break;
        default:
            cpuMessage(info());
            throw CpuError("Unknown opcode at address "s + SegmentedAddress(reg16(REG_CS), reg16(REG_IP)).toString() + ": " + hexVal(op));
        }
    }
}

void Cpu_8086::resetRegs() {
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
    static Register const id[] = { REG_AL, REG_CL, REG_DL, REG_BL, REG_AH, REG_CH, REG_DH, REG_BH } ;
    regs_.reg8[id[dst]] = val;
}
