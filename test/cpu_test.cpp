#include "cpu.h"
#include "test/debug.h"
#include "gtest/gtest.h"

using namespace std;

TEST(Cpu_8086, OrderHL) {
    Cpu_8086 cpu(nullptr);
    cpu.regs_.AX = 0x4c00;
    TRACELN(std::hex << "ax = 0x" << (int)cpu.regs_.AX << ", ah = 0x" << (int)cpu.regs_.AH << ", al = 0x" << (int)cpu.regs_.AL << std::dec);
    ASSERT_EQ(cpu.regs_.AH, 0x4c);
    ASSERT_EQ(cpu.regs_.AL, 0x00);
}

TEST(Cpu_8086, Flags) {
    Cpu_8086 cpu(nullptr);

    cpu.regs_.FLAGS = 0x3102;
    TRACELN(std::hex
         << "FLAGS = 0x" << cpu.regs_.FLAGS << ", "
         << "CF = "  << (int)cpu.regs_.CF  << ", "
         << "B1 = "  << (int)cpu.regs_.B1  << ", "
         << "PF = "  << (int)cpu.regs_.PF  << ", "
         << "B3 = "  << (int)cpu.regs_.B3  << ", "
         << "AF = "  << (int)cpu.regs_.AF  << ", "
         << "B5 = "  << (int)cpu.regs_.B5  << ", "
         << "ZF = "  << (int)cpu.regs_.ZF  << ", "
         << "SF = "  << (int)cpu.regs_.SF  << ", "
         << "TF = "  << (int)cpu.regs_.TF  << ", "
         << "IF = "  << (int)cpu.regs_.IF  << ", "
         << "DF = "  << (int)cpu.regs_.DF  << ", "
         << "OF = "  << (int)cpu.regs_.OF  << ", "
         << "B12 = " << (int)cpu.regs_.B12 << ", "
         << "B13 = " << (int)cpu.regs_.B13 << ", "
         << "B14 = " << (int)cpu.regs_.B14 << ", "
         << "B15 = " << (int)cpu.regs_.B15) << std::dec;

    ASSERT_EQ(cpu.regs_.CF, 0);
    ASSERT_EQ(cpu.regs_.B1, 1);
    ASSERT_EQ(cpu.regs_.PF, 0);
    ASSERT_EQ(cpu.regs_.B3, 0);
    ASSERT_EQ(cpu.regs_.AF, 0);
    ASSERT_EQ(cpu.regs_.B5, 0);
    ASSERT_EQ(cpu.regs_.ZF, 0);
    ASSERT_EQ(cpu.regs_.SF, 0);
    ASSERT_EQ(cpu.regs_.TF, 1);
    ASSERT_EQ(cpu.regs_.IF, 0);
    ASSERT_EQ(cpu.regs_.DF, 0);
    ASSERT_EQ(cpu.regs_.OF, 0);
    ASSERT_EQ(cpu.regs_.B12, 1);
    ASSERT_EQ(cpu.regs_.B13, 1);
    ASSERT_EQ(cpu.regs_.B14, 0);
    ASSERT_EQ(cpu.regs_.B15, 0);
}
