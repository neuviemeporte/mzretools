#include <iostream>

#include "cpu.h"
#include "gtest/gtest.h"

using namespace std;

TEST(Cpu, OrderHL) {
    Cpu cpu;
    cpu.regs.AX = 0x4c00;
    cout << std::hex << "ax = 0x" << (int)cpu.regs.AX << ", ah = 0x" << (int)cpu.regs.AH << ", al = 0x" << (int)cpu.regs.AL << endl;
    ASSERT_EQ(cpu.regs.AH, 0x4c);
    ASSERT_EQ(cpu.regs.AL, 0x00);
}

TEST(Cpu, Flags) {
    Cpu cpu;

    cpu.regs.FLAGS = 0x3102;
    cout << std::hex
         << "FLAGS = 0x" << cpu.regs.FLAGS << endl
         << "CF == "  << (int)cpu.regs.CF << endl
         << "B1 == "  << (int)cpu.regs.B1 << endl
         << "PF == "  << (int)cpu.regs.PF << endl
         << "B3 == "  << (int)cpu.regs.B3 << endl
         << "AF == "  << (int)cpu.regs.AF << endl
         << "B5 == "  << (int)cpu.regs.B5 << endl
         << "ZF == "  << (int)cpu.regs.ZF << endl
         << "SF == "  << (int)cpu.regs.SF << endl
         << "TF == "  << (int)cpu.regs.TF << endl
         << "IF == "  << (int)cpu.regs.IF << endl
         << "DF == "  << (int)cpu.regs.DF << endl
         << "OF == "  << (int)cpu.regs.OF << endl
         << "B12 == " << (int)cpu.regs.B12 << endl
         << "B13 == " << (int)cpu.regs.B13 << endl
         << "B14 == " << (int)cpu.regs.B14 << endl
         << "B15 == " << (int)cpu.regs.B15 << endl;

    assert(cpu.regs.CF == 0);
    assert(cpu.regs.B1 == 1);
    assert(cpu.regs.PF == 0);
    assert(cpu.regs.B3 == 0);
    assert(cpu.regs.AF == 0);
    assert(cpu.regs.B5 == 0);
    assert(cpu.regs.ZF == 0);
    assert(cpu.regs.SF == 0);
    assert(cpu.regs.TF == 1);
    assert(cpu.regs.IF == 0);
    assert(cpu.regs.DF == 0);
    assert(cpu.regs.OF == 0);
    assert(cpu.regs.B12 == 1);
    assert(cpu.regs.B13 == 1);
    assert(cpu.regs.B14 == 0);
    assert(cpu.regs.B15 == 0);
}
