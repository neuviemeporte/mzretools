#include "debug.h"
#include "gtest/gtest.h"
#include "dos/memory.h"
#include "dos/cpu.h"

using namespace std;

class Cpu_8086_Test : public ::testing::Test {
protected:
    Cpu_8086 *cpu_;
    Memory *mem_;
    Registers *regs_;

protected:
    void SetUp() override {
        mem_ = new Memory();
        cpu_ = new Cpu_8086(mem_, nullptr);
        regs_ = &cpu_->regs_;
    }

    void TearDown() override {
        if (HasFailure()) {
            cout << cpu_->info() << endl;
        }
        delete cpu_;
        delete mem_;
    }

    string info() { return cpu_->info(); }
    void setupCode(const Byte *code, const Size size) {
        const Offset
            memStartLinear = mem_->freeStart(),
            memEndLinear = mem_->freeEnd();
        const Address 
            memStart(memStartLinear),
            memEnd(memEndLinear);
        mem_->writeBuf(memStartLinear, code, size);
        cpu_->init(memStart, memEnd);
    }
};

TEST_F(Cpu_8086_Test, OrderHL) {
    regs_->bit16(REG_AX) = 0x4c7b;
    regs_->bit16(REG_BX) = 0x1234;
    regs_->bit16(REG_CX) = 0x0b0c;
    regs_->bit16(REG_DX) = 0xfefc;
    TRACELN(regs_->dump());
    ASSERT_EQ(regs_->bit8(REG_AH), 0x4c);
    ASSERT_EQ(regs_->bit8(REG_AL), 0x7b);
    ASSERT_EQ(regs_->bit8(REG_BH), 0x12);
    ASSERT_EQ(regs_->bit8(REG_BL), 0x34);
    ASSERT_EQ(regs_->bit8(REG_CH), 0x0b);
    ASSERT_EQ(regs_->bit8(REG_CL), 0x0c);
    ASSERT_EQ(regs_->bit8(REG_DH), 0xfe);
    ASSERT_EQ(regs_->bit8(REG_DL), 0xfc);    
}

TEST_F(Cpu_8086_Test, Flags) {
    auto flagset = vector<Flag>{ 
        FLAG_CARRY, FLAG_B1, FLAG_PARITY, FLAG_B3, FLAG_AUXC, FLAG_B5, FLAG_ZERO, FLAG_SIGN, 
        FLAG_TRAP, FLAG_INT, FLAG_DIR, FLAG_OVER, FLAG_B12, FLAG_B13, FLAG_B14, FLAG_B15
    };
    for (auto flag : flagset) {
        regs_->setFlag(flag, true);
        ASSERT_TRUE(regs_->getFlag(flag));
    }
    ASSERT_EQ(regs_->bit16(REG_FLAGS), 0xffff);
    for (auto flag : flagset) {
        regs_->setFlag(flag, false);
        ASSERT_FALSE(regs_->getFlag(flag));
    }    
    ASSERT_EQ(regs_->bit16(REG_FLAGS), 0x0);
    for (auto flag : flagset) {
        regs_->setFlag(flag, true);
        ASSERT_TRUE(regs_->getFlag(flag));
        regs_->setFlag(flag, false);
        ASSERT_FALSE(regs_->getFlag(flag));
    }
    ASSERT_EQ(regs_->bit16(REG_FLAGS), 0x0);
}

TEST_F(Cpu_8086_Test, Arithmetic) {
    // TODO: implement an assembler to generate code from a vector of structs
    const Byte code[] = {
        0xB0, 0xFF, // mov al,0xff
        0x04, 0x01, // add al,0x1
        0xb0, 0x02, // mov al,0x2
        0x04, 0x03, // add al,0x3
        0xB0, 0x78, // mov al,120
        0x04, 0x0a, // add al,10
        0xB0, 0x00, // mov al,0x0
        0x2C, 0x01, // sub al,0x1        
    };
    //                  XXXXODITSZXAXPXC
    const Word mask = 0b0000100011010101;
    setupCode(code, sizeof(code));
    ASSERT_EQ(regs_->bit16(REG_IP), 0);
    cpu_->step(); // mov al, 0xff
    ASSERT_EQ(regs_->bit16(REG_IP), 2);
    ASSERT_EQ(regs_->bit8(REG_AL), 0xff);
    cpu_->step(); // add al, 0x1
    ASSERT_EQ(regs_->bit16(REG_IP), 4);
    ASSERT_EQ(regs_->bit8(REG_AL), 0x00);
    ASSERT_EQ(regs_->bit16(REG_FLAGS) & mask, 0b1010101); // C1 Z1 S0 O0 A1 P1
    cpu_->step();
    ASSERT_EQ(regs_->bit16(REG_IP), 6);
    ASSERT_EQ(regs_->bit8(REG_AL), 0x02);
    cpu_->step();
    ASSERT_EQ(regs_->bit16(REG_IP), 8);
    ASSERT_EQ(regs_->bit8(REG_AL), 0x05);
    ASSERT_EQ(regs_->bit16(REG_FLAGS) & mask, 0b100); // C0 Z0 S0 O0 A0 P1
    cpu_->step(); // mov al,120
    ASSERT_EQ(regs_->bit16(REG_IP), 10);
    ASSERT_EQ(regs_->bit8(REG_AL), 120);
    cpu_->step(); // add al,10
    ASSERT_EQ(regs_->bit16(REG_IP), 12);
    ASSERT_EQ(regs_->bit8(REG_AL), 130);
    ASSERT_EQ(regs_->bit16(REG_FLAGS) & mask, 0b100010010100); // C0 Z0 S1 O1 A1 P1
    cpu_->step(); // mov al,0x0
    ASSERT_EQ(regs_->bit16(REG_IP), 14);
    ASSERT_EQ(regs_->bit8(REG_AL), 0x0);
    cpu_->step(); // sub al,0x1
    ASSERT_EQ(regs_->bit16(REG_IP), 16);
    ASSERT_EQ(regs_->bit8(REG_AL), 0xff);
    ASSERT_EQ(regs_->bit16(REG_FLAGS) & mask, 0b10010101); // C1 Z0 S1 O0 A1 P1
}

TEST_F(Cpu_8086_Test, WordOperand) {
    const Byte code[] = {
        0xbf, 0x6f, 0x01 // mov di,0x16f
    };
    setupCode(code, sizeof(code));
    cpu_->step();
    ASSERT_EQ(regs_->bit16(REG_DI), 0x16f);
}

TEST_F(Cpu_8086_Test, MovEvGv) {
    const Byte code[] = {
        0x8b, 0x36, 0x02, 0x00 // mov si,[0x2]
    };
    setupCode(code, sizeof(code));
    mem_->writeWord(Address(regs_->bit16(REG_DS), 2).toLinear(), 0x1234);
    cpu_->step();
    ASSERT_EQ(regs_->bit16(REG_SI), 0x1234);
}