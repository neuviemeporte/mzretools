#include "cpu.h"
#include "test/debug.h"
#include "gtest/gtest.h"

using namespace std;

TEST(Cpu_8086, OrderHL) {
    Cpu_8086 cpu{nullptr, nullptr};
    cpu.reg16(REG_AX) = 0x4c7b;
    cpu.reg16(REG_BX) = 0x1234;
    cpu.reg16(REG_CX) = 0x0b0c;
    cpu.reg16(REG_DX) = 0xfefc;
    TRACELN(cpu.info());
    ASSERT_EQ(cpu.reg8(REG_AH), 0x4c);
    ASSERT_EQ(cpu.reg8(REG_AL), 0x7b);
    ASSERT_EQ(cpu.reg8(REG_BH), 0x12);
    ASSERT_EQ(cpu.reg8(REG_BL), 0x34);
    ASSERT_EQ(cpu.reg8(REG_CH), 0x0b);
    ASSERT_EQ(cpu.reg8(REG_CL), 0x0c);
    ASSERT_EQ(cpu.reg8(REG_DH), 0xfe);
    ASSERT_EQ(cpu.reg8(REG_DL), 0xfc);    
}

TEST(Cpu_8086, Flags) {
    Cpu_8086 cpu{nullptr, nullptr};

    cpu.reg16(REG_FLAGS) = 0x3102;
    TRACELN(std::hex
         << "FLAGS = 0x" << cpu.reg16(REG_FLAGS) << ", "
         << "CF = " << cpu.getFlag(FLAG_CF) << ", "
         << "B1 = " << cpu.getFlag(FLAG_B1) << ", "
         << "PF = " << cpu.getFlag(FLAG_PF) << ", "
         << "B3 = " << cpu.getFlag(FLAG_B3) << ", "
         << "AF = " << cpu.getFlag(FLAG_AF) << ", "
         << "B5 = " << cpu.getFlag(FLAG_B5) << ", "
         << "ZF = " << cpu.getFlag(FLAG_ZF) << ", "
         << "SF = " << cpu.getFlag(FLAG_SF) << ", "
         << "TF = " << cpu.getFlag(FLAG_TF) << ", "
         << "IF = " << cpu.getFlag(FLAG_IF) << ", "
         << "DF = " << cpu.getFlag(FLAG_DF) << ", "
         << "OF = " << cpu.getFlag(FLAG_OF) << ", "
         << "B12 = "<< cpu.getFlag(FLAG_B12) << ", "
         << "B13 = "<< cpu.getFlag(FLAG_B13) << ", "
         << "B14 = "<< cpu.getFlag(FLAG_B14) << ", "
         << "B15 = "<< cpu.getFlag(FLAG_B15) << std::dec);

    ASSERT_FALSE(cpu.getFlag(FLAG_CF));
    ASSERT_TRUE(cpu.getFlag(FLAG_B1));
    ASSERT_FALSE(cpu.getFlag(FLAG_PF));
    ASSERT_FALSE(cpu.getFlag(FLAG_B3));
    ASSERT_FALSE(cpu.getFlag(FLAG_AF));
    ASSERT_FALSE(cpu.getFlag(FLAG_B5));
    ASSERT_FALSE(cpu.getFlag(FLAG_ZF));
    ASSERT_FALSE(cpu.getFlag(FLAG_SF));
    ASSERT_TRUE(cpu.getFlag(FLAG_TF));
    ASSERT_FALSE(cpu.getFlag(FLAG_IF));
    ASSERT_FALSE(cpu.getFlag(FLAG_DF));
    ASSERT_FALSE(cpu.getFlag(FLAG_OF));
    ASSERT_TRUE(cpu.getFlag(FLAG_B12));
    ASSERT_TRUE(cpu.getFlag(FLAG_B13));
    ASSERT_FALSE(cpu.getFlag(FLAG_B14));
    ASSERT_FALSE(cpu.getFlag(FLAG_B15));

    cpu.setFlag(FLAG_B5, true);
    ASSERT_TRUE(cpu.getFlag(FLAG_B5));
    cpu.setFlag(FLAG_TF, false);
    ASSERT_FALSE(cpu.getFlag(FLAG_TF));
}
