#include "cpu.h"
#include "test/debug.h"
#include "gtest/gtest.h"

using namespace std;

TEST(Cpu, OrderHL) {
    Cpu cpu;
    cpu.regs.AX = 0x4c00;
    TRACELN(std::hex << "ax = 0x" << (int)cpu.regs.AX << ", ah = 0x" << (int)cpu.regs.AH << ", al = 0x" << (int)cpu.regs.AL << std::dec);
    ASSERT_EQ(cpu.regs.AH, 0x4c);
    ASSERT_EQ(cpu.regs.AL, 0x00);
}

TEST(Cpu, Flags) {
    Cpu cpu;

    cpu.regs.FLAGS = 0x3102;
    TRACELN(std::hex
         << "FLAGS = 0x" << cpu.regs.FLAGS << ", "
         << "CF = "  << (int)cpu.regs.CF  << ", "
         << "B1 = "  << (int)cpu.regs.B1  << ", "
         << "PF = "  << (int)cpu.regs.PF  << ", "
         << "B3 = "  << (int)cpu.regs.B3  << ", "
         << "AF = "  << (int)cpu.regs.AF  << ", "
         << "B5 = "  << (int)cpu.regs.B5  << ", "
         << "ZF = "  << (int)cpu.regs.ZF  << ", "
         << "SF = "  << (int)cpu.regs.SF  << ", "
         << "TF = "  << (int)cpu.regs.TF  << ", "
         << "IF = "  << (int)cpu.regs.IF  << ", "
         << "DF = "  << (int)cpu.regs.DF  << ", "
         << "OF = "  << (int)cpu.regs.OF  << ", "
         << "B12 = " << (int)cpu.regs.B12 << ", "
         << "B13 = " << (int)cpu.regs.B13 << ", "
         << "B14 = " << (int)cpu.regs.B14 << ", "
         << "B15 = " << (int)cpu.regs.B15) << std::dec;

    ASSERT_EQ(cpu.regs.CF, 0);
    ASSERT_EQ(cpu.regs.B1, 1);
    ASSERT_EQ(cpu.regs.PF, 0);
    ASSERT_EQ(cpu.regs.B3, 0);
    ASSERT_EQ(cpu.regs.AF, 0);
    ASSERT_EQ(cpu.regs.B5, 0);
    ASSERT_EQ(cpu.regs.ZF, 0);
    ASSERT_EQ(cpu.regs.SF, 0);
    ASSERT_EQ(cpu.regs.TF, 1);
    ASSERT_EQ(cpu.regs.IF, 0);
    ASSERT_EQ(cpu.regs.DF, 0);
    ASSERT_EQ(cpu.regs.OF, 0);
    ASSERT_EQ(cpu.regs.B12, 1);
    ASSERT_EQ(cpu.regs.B13, 1);
    ASSERT_EQ(cpu.regs.B14, 0);
    ASSERT_EQ(cpu.regs.B15, 0);
}
