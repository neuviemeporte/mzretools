#include <vector>
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "debug.h"
#include "dos/memory.h"
#include "dos/cpu.h"
#include "dos/opcodes.h"
#include "dos/modrm.h"
#include "dos/util.h"
#include "dos/interrupt.h"

using namespace std;
using ::testing::_;
using ::testing::Return;

class MockInterruptHandler : public InterruptInterface {
public:
    MOCK_METHOD(IntStatus, interrupt, (const Byte num, Registers &regs), (override));
};

class Cpu_8086_Test : public ::testing::Test {
protected:
    Cpu_8086 *cpu_;
    Memory *mem_;
    Registers *regs_;
    MockInterruptHandler *int_;

protected:
    void SetUp() override {
        mem_ = new Memory();
        int_ = new MockInterruptHandler();
        cpu_ = new Cpu_8086(mem_, int_);
        regs_ = &cpu_->regs_;
    }

    void TearDown() override {
        if (HasFailure()) {
            TRACELN(cpu_->info());
        }
        delete cpu_;
        delete int_;
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
        cpu_->init(memStart, memEnd, 0);
    }

    inline Byte& reg8(const Register reg) { return regs_->bit8(reg); }
    inline Word& reg16(const Register reg) { return regs_->bit16(reg); }
    inline bool flag(const Flag flag) { return regs_->getFlag(flag); }
    inline Word instructionLength() const { return cpu_->instructionLength(); }
};

TEST_F(Cpu_8086_Test, OrderHL) {
    reg16(REG_AX) = 0x4c7b;
    reg16(REG_BX) = 0x1234;
    reg16(REG_CX) = 0x0b0c;
    reg16(REG_DX) = 0xfefc;
    TRACELN(regs_->dump());
    ASSERT_EQ(reg8(REG_AH), 0x4c);
    ASSERT_EQ(reg8(REG_AL), 0x7b);
    ASSERT_EQ(reg8(REG_BH), 0x12);
    ASSERT_EQ(reg8(REG_BL), 0x34);
    ASSERT_EQ(reg8(REG_CH), 0x0b);
    ASSERT_EQ(reg8(REG_CL), 0x0c);
    ASSERT_EQ(reg8(REG_DH), 0xfe);
    ASSERT_EQ(reg8(REG_DL), 0xfc);    
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
    ASSERT_EQ(reg16(REG_FLAGS), 0xffff);
    for (auto flag : flagset) {
        regs_->setFlag(flag, false);
        ASSERT_FALSE(regs_->getFlag(flag));
    }    
    ASSERT_EQ(reg16(REG_FLAGS), 0x0);
    for (auto flag : flagset) {
        regs_->setFlag(flag, true);
        ASSERT_TRUE(regs_->getFlag(flag));
        regs_->setFlag(flag, false);
        ASSERT_FALSE(regs_->getFlag(flag));
    }
    ASSERT_EQ(reg16(REG_FLAGS), 0x0);
}

TEST_F(Cpu_8086_Test, DISABLED_Arithmetic) {
    // TODO: implement an assembler to generate code from a vector of structs
    const Byte code[] = {
        OP_MOV_AL_Ib, 0xFF, // mov al,0xff
        OP_ADD_AL_Ib, 0x01, // add al,0x1
        OP_MOV_AL_Ib, 0x02, // mov al,0x2
        OP_ADD_AL_Ib, 0x03, // add al,0x3
        OP_MOV_AL_Ib, 0x78, // mov al,120
        OP_ADD_AL_Ib, 0x0a, // add al,10
        OP_MOV_AL_Ib, 0x00, // mov al,0x0
        OP_SUB_AL_Ib, 0x01, // sub al,0x1        
    };
    //                  XXXXODITSZXAXPXC
    const Word mask = 0b0000100011010101;
    setupCode(code, sizeof(code));
    ASSERT_EQ(reg16(REG_IP), 0);
    cpu_->step(); // mov al, 0xff
    ASSERT_EQ(reg16(REG_IP), 2);
    ASSERT_EQ(reg8(REG_AL), 0xff);
    cpu_->step(); // add al, 0x1
    ASSERT_EQ(reg16(REG_IP), 4);
    ASSERT_EQ(reg8(REG_AL), 0x00);
    ASSERT_EQ(reg16(REG_FLAGS) & mask, 0b1010101); // C1 Z1 S0 O0 A1 P1
    cpu_->step();
    ASSERT_EQ(reg16(REG_IP), 6);
    ASSERT_EQ(reg8(REG_AL), 0x02);
    cpu_->step();
    ASSERT_EQ(reg16(REG_IP), 8);
    ASSERT_EQ(reg8(REG_AL), 0x05);
    ASSERT_EQ(reg16(REG_FLAGS) & mask, 0b100); // C0 Z0 S0 O0 A0 P1
    cpu_->step(); // mov al,120
    ASSERT_EQ(reg16(REG_IP), 10);
    ASSERT_EQ(reg8(REG_AL), 120);
    cpu_->step(); // add al,10
    ASSERT_EQ(reg16(REG_IP), 12);
    ASSERT_EQ(reg8(REG_AL), 130);
    ASSERT_EQ(reg16(REG_FLAGS) & mask, 0b100010010100); // C0 Z0 S1 O1 A1 P1
    cpu_->step(); // mov al,0x0
    ASSERT_EQ(reg16(REG_IP), 14);
    ASSERT_EQ(reg8(REG_AL), 0x0);
    cpu_->step(); // sub al,0x1
    ASSERT_EQ(reg16(REG_IP), 16);
    ASSERT_EQ(reg8(REG_AL), 0xff);
    ASSERT_EQ(reg16(REG_FLAGS) & mask, 0b10010101); // C1 Z0 S1 O0 A1 P1
}

TEST_F(Cpu_8086_Test, WordOperand) {
    const Byte code[] = {
        0xbf, 0x6f, 0x01 // mov di,0x16f
    };
    setupCode(code, sizeof(code));
    cpu_->step();
    ASSERT_EQ(reg16(REG_DI), 0x16f);
}

// test all combinations of the mov opcodes with modrm modificator bytes
TEST_F(Cpu_8086_Test, Mov) {
    const Byte disp1 = 0xa, disp2 = 0x8c;
    Offset off;
    const Byte code[] = {
        OP_MOV_Eb_Gb, MODRM_MOD_NODISP | MODRM_REG_AH | MODRM_MEM_BP_DI, // mov [bp+di],ah
        OP_MOV_Gb_Eb, MODRM_MOD_DISP8 | MODRM_REG_CL | MODRM_MEM_BX_SI_OFF, disp1, // mov cl,[bx+si+0xa]
        OP_MOV_Ev_Gv, MODRM_MOD_DISP16 | MODRM_REG_DX | MODRM_MEM_SI_OFF, disp2, disp1, // mov [si+0xa8c],dx
        OP_MOV_Gv_Ev, MODRM_MOD_REG | MODRM_REG_SI | MODRM_REG_CX >> MODRM_REG_SHIFT, // mov si,cx
        OP_MOV_Ew_Sw, MODRM_MOD_NODISP | MODRM_REG_ES | MODRM_MEM_BX, // mov [bx],es
        OP_MOV_Sw_Ew, MODRM_MOD_DISP8 | MODRM_REG_SS | MODRM_MEM_DI_OFF, disp2, // mov ss,[di-0x74]
        OP_MOV_AL_Ob, 0xab, 0xcd, // mov al,[0xcdab]
        OP_MOV_AX_Ov, 0xcd, 0xab, // mov ax,[0xabcd]
        OP_MOV_Ob_AL, 0xcd, 0xab, // mov [0xabcd],al
        OP_MOV_Ov_AX, 0xab, 0xcd, // mov [0xcdab],ax
        OP_MOV_AL_Ib, 0x66, // mov al,0x66
        OP_MOV_CL_Ib, 0x67, // mov cl,0x67
        OP_MOV_DL_Ib, 0x68, // mov dl,0x68
        OP_MOV_BL_Ib, 0x69, // mov bl,0x69
        OP_MOV_AH_Ib, 0x6a, // mov ah,0x6a
        OP_MOV_CH_Ib, 0x6b, // mov ch,0x6b
        OP_MOV_DH_Ib, 0x6c, // mov dh,0x6c
        OP_MOV_BH_Ib, 0x6d, // mov bh,0x6d
        OP_MOV_AX_Iv, 0x34, 0x12, // mov ax,0x1234
        OP_MOV_CX_Iv, 0x35, 0x12, // mov cx,0x1235
        OP_MOV_DX_Iv, 0x36, 0x12, // mov dx,0x1236
        OP_MOV_BX_Iv, 0x37, 0x12, // mov bx,0x1237
        OP_MOV_SP_Iv, 0x38, 0x12, // mov sp,0x1238
        OP_MOV_BP_Iv, 0x39, 0x12, // mov bp,0x1239
        OP_MOV_SI_Iv, 0x3a, 0x12, // mov si,0x123a
        OP_MOV_DI_Iv, 0x3b, 0x12, // mov di,0x123b
        OP_MOV_Eb_Ib, MODRM_MOD_DISP16 | MODRM_REG_NONE | MODRM_MEM_BX_DI_OFF, 0x24, 0x10, 0xab,  // mov [bx+di+0x1024],0xab
        OP_MOV_Ev_Iv, MODRM_MOD_NODISP | MODRM_REG_NONE | MODRM_MEM_ADDR, 0xbe, 0xba, 0xcd, 0xab, // mov [0xbabe],0xabcd
        OP_MOV_Eb_Ib, MODRM_MOD_DISP16 | MODRM_REG_NONE | MODRM_MEM_BX_DI_OFF, 0xff, 0xfe, 0xab, // mov [bx+di-0x101], 0xab
        OP_PREFIX_ES, OP_MOV_Eb_Gb, MODRM_MOD_NODISP | MODRM_REG_CH | MODRM_MEM_BX_DI, // mov [es:bx+di],ch
    };
    writeBinaryFile("mov.bin", code, sizeof(code));
    setupCode(code, sizeof(code));

    reg8(REG_AH) = 69;
    reg16(REG_BP) = 123;
    reg16(REG_DI) = 456;
    reg16(REG_SS) = 789;
    off = SEG_OFFSET(reg16(REG_SS)) + reg16(REG_BP) + reg16(REG_DI);
    cpu_->step(); // mov [bp+di],ah
    ASSERT_EQ(mem_->readByte(off), reg8(REG_AH));

    reg16(REG_BX) = 639;
    reg16(REG_SI) = 157;
    reg16(REG_DS) = 371;
    off = SEG_OFFSET(reg16(REG_DS)) + reg16(REG_BX) + reg16(REG_SI) + 0xa;
    mem_->writeByte(off, 201);
    cpu_->step(); // mov cl,[bx+si+0xa]
    ASSERT_EQ(reg8(REG_CL), 201);

    reg16(REG_DX) = 4265;
    off = SEG_OFFSET(reg16(REG_DS)) + reg16(REG_SI) + 0xa8c;
    cpu_->step(); // mov [si+0xa8c],dx
    ASSERT_EQ(mem_->readWord(off), reg16(REG_DX));

    reg16(REG_CX) = 7488;
    cpu_->step(); // mov si,cx
    ASSERT_EQ(reg16(REG_SI), reg16(REG_CX));

    reg16(REG_ES) = 6548;
    off = SEG_OFFSET(reg16(REG_DS)) + reg16(REG_BX);
    cpu_->step(); // mov [bx],es
    ASSERT_EQ(mem_->readWord(off), reg16(REG_ES));

    off = SEG_OFFSET(reg16(REG_ES)) + reg16(REG_DI) - 0x74;
    mem_->writeWord(off, 0xfafe);
    cpu_->step(); // mov ss,[di+0x8c]
    ASSERT_EQ(reg16(REG_SS), 0xfafe);

    off = SEG_OFFSET(reg16(REG_DS)) + 0xcdab;
    mem_->writeByte(off, 0x64);
    cpu_->step(); // mov al,[0xcdab]
    ASSERT_EQ(reg8(REG_AL), 0x64);

    off = SEG_OFFSET(reg16(REG_DS)) + 0xabcd;
    mem_->writeWord(off, 0xcaca);
    cpu_->step(); // mov ax,[0xabcd]
    ASSERT_EQ(reg16(REG_AX), 0xcaca);

    off = SEG_OFFSET(reg16(REG_DS)) + 0xabcd;
    cpu_->step(); // mov [0xabcd],al
    ASSERT_EQ(mem_->readByte(off), reg8(REG_AL));

    off = SEG_OFFSET(reg16(REG_DS)) + 0xcdab;
    cpu_->step(); // mov [0xcdab],ax
    ASSERT_EQ(mem_->readWord(off), reg16(REG_AX));

    cpu_->step(); // mov al,0x66
    ASSERT_EQ(reg8(REG_AL), 0x66);
    cpu_->step(); // mov cl,0x67
    ASSERT_EQ(reg8(REG_CL), 0x67);
    cpu_->step(); // mov dl,0x68
    ASSERT_EQ(reg8(REG_DL), 0x68);
    cpu_->step(); // mov bl,0x69
    ASSERT_EQ(reg8(REG_BL), 0x69);
    cpu_->step(); // mov ah,0x6a
    ASSERT_EQ(reg8(REG_AH), 0x6a);
    cpu_->step(); // mov ch,0x6b
    ASSERT_EQ(reg8(REG_CH), 0x6b);
    cpu_->step(); // mov dh,0x6c
    ASSERT_EQ(reg8(REG_DH), 0x6c);
    cpu_->step(); // mov bh,0x6d
    ASSERT_EQ(reg8(REG_BH), 0x6d);

    cpu_->step(); // mov ax,0x1234
    ASSERT_EQ(reg16(REG_AX), 0x1234);
    cpu_->step(); // mov cx,0x1235
    ASSERT_EQ(reg16(REG_CX), 0x1235);
    cpu_->step(); // mov dx,0x1236
    ASSERT_EQ(reg16(REG_DX), 0x1236);
    cpu_->step(); // mov bx,0x1237
    ASSERT_EQ(reg16(REG_BX), 0x1237);
    cpu_->step(); // mov sp,0x1238
    ASSERT_EQ(reg16(REG_SP), 0x1238);
    cpu_->step(); // mov bp,0x1239
    ASSERT_EQ(reg16(REG_BP), 0x1239);
    cpu_->step(); // mov si,0x123a
    ASSERT_EQ(reg16(REG_SI), 0x123a);
    cpu_->step(); // mov di,0x123b
    ASSERT_EQ(reg16(REG_DI), 0x123b);

    off = SEG_OFFSET(reg16(REG_DS)) + reg16(REG_BX) + reg16(REG_DI) + 0x1024;
    cpu_->step(); // mov [bx+di+0x1024],0xab
    ASSERT_EQ(mem_->readByte(off), 0xab);

    off = SEG_OFFSET(reg16(REG_DS)) + 0xbabe;
    cpu_->step(); // mov [0xbabe],0xabcd
    ASSERT_EQ(mem_->readWord(off), 0xabcd);

    // negative displacement value
    off = SEG_OFFSET(reg16(REG_DS)) + reg16(REG_BX) + reg16(REG_DI) - 0x101;
    cpu_->step(); // mov [bx+di-0x101], 0xab
    ASSERT_EQ(mem_->readByte(off), 0xab);

    // segment override prefix
    off = SEG_OFFSET(reg16(REG_ES)) + reg16(REG_BX) + reg16(REG_DI);
    cpu_->step(); // mov [es:bx+di],ch
    ASSERT_EQ(mem_->readByte(off), reg8(REG_CH));
}

TEST_F(Cpu_8086_Test, Int) {
    const Byte code[] = {
        OP_INT_3,
        OP_INT_Ib, 0x21,
        OP_INTO,
        OP_INTO,
    };
    setupCode(code, sizeof(code));

    EXPECT_CALL(*int_, interrupt(3, _)).Times(1).WillOnce(Return(INT_OK));
    cpu_->step(); // int 3

    EXPECT_CALL(*int_, interrupt(0x21, _)).Times(1).WillOnce(Return(INT_OK));
    cpu_->step(); // int 0x21

    EXPECT_CALL(*int_, interrupt(_, _)).Times(0);
    regs_->setFlag(FLAG_OVER, false);
    cpu_->step(); // into

    EXPECT_CALL(*int_, interrupt(4, _)).Times(1).WillOnce(Return(INT_OK));
    regs_->setFlag(FLAG_OVER, true);
    cpu_->step(); // into    
}

TEST_F(Cpu_8086_Test, CmpSub) {
    Byte code[] = {
        OP_SUB_Gv_Ev, MODRM_MOD_REG | MODRM_REG_AX | MODRM_REG_BX >> MODRM_REG_SHIFT,  // sub, ax,bx
    };
    SWord result[] = { 123-3, 123+3, -123-3, -123+3, 123, 123,-123,-123 };

    for (int i = 0; i < 2; ++i) {
        setupCode(code, sizeof(code));
        reg16(REG_AX) = 123;
        reg16(REG_BX) = 3;
        cpu_->step();
        ASSERT_EQ(reg16(REG_AX), result[0+i*4]);
        ASSERT_FALSE(flag(FLAG_CARRY));
        ASSERT_FALSE(flag(FLAG_ZERO));
        ASSERT_FALSE(flag(FLAG_SIGN));
        ASSERT_FALSE(flag(FLAG_OVER));
        ASSERT_FALSE(flag(FLAG_AUXC));
        ASSERT_TRUE(flag(FLAG_PARITY));

        setupCode(code, sizeof(code));
        reg16(REG_AX) = 123;
        reg16(REG_BX) = WORD_SIGNED(-3);
        cpu_->step();
        ASSERT_EQ(reg16(REG_AX), result[1+i*4]);
        ASSERT_TRUE(flag(FLAG_CARRY));
        ASSERT_FALSE(flag(FLAG_ZERO));
        ASSERT_FALSE(flag(FLAG_SIGN));
        ASSERT_FALSE(flag(FLAG_OVER));
        ASSERT_TRUE(flag(FLAG_AUXC));
        ASSERT_TRUE(flag(FLAG_PARITY));

        setupCode(code, sizeof(code));
        reg16(REG_AX) = WORD_SIGNED(-123);
        reg16(REG_BX) = 3;
        cpu_->step();
        ASSERT_EQ(WORD_SIGNED(reg16(REG_AX)), result[2+i*4]);
        ASSERT_FALSE(flag(FLAG_CARRY));
        ASSERT_FALSE(flag(FLAG_ZERO));
        ASSERT_TRUE(flag(FLAG_SIGN));
        ASSERT_FALSE(flag(FLAG_OVER));
        ASSERT_FALSE(flag(FLAG_AUXC));
        ASSERT_TRUE(flag(FLAG_PARITY));

        setupCode(code, sizeof(code));
        reg16(REG_AX) = WORD_SIGNED(-123);
        reg16(REG_BX) = WORD_SIGNED(-3);
        cpu_->step();
        ASSERT_EQ(WORD_SIGNED(reg16(REG_AX)), result[3+i*4]);
        ASSERT_TRUE(flag(FLAG_CARRY));
        ASSERT_FALSE(flag(FLAG_ZERO));
        ASSERT_TRUE(flag(FLAG_SIGN));
        ASSERT_FALSE(flag(FLAG_OVER));
        ASSERT_TRUE(flag(FLAG_AUXC));
        ASSERT_TRUE(flag(FLAG_PARITY));

        code[0] = OP_CMP_Gv_Ev;
    }

}


TEST_F(Cpu_8086_Test, Word) {
    Word x = 0x165;
    SByte b = 0xf7;
    x += b;
    TRACELN("x = " << hexVal(x));
}

// TODO: implement tests for group opcodes