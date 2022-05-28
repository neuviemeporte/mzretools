#include <vector>
#include "debug.h"
#include "gtest/gtest.h"
#include "dos/memory.h"
#include "dos/cpu.h"
#include "dos/opcodes.h"
#include "dos/modrm.h"
#include "dos/util.h"

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

    inline Byte& reg8(const Register reg) { return regs_->bit8(reg); }
    inline Word& reg16(const Register reg) { return regs_->bit16(reg); }
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

TEST_F(Cpu_8086_Test, Arithmetic) {
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
        OP_MOV_Sw_Ew, MODRM_MOD_DISP8 | MODRM_REG_SS | MODRM_MEM_DI_OFF, disp2, // mov ss,[di+0x8c]
        OP_MOV_AL_Ob, 0xab, 0xcd, // mov al,[0xcdab]
        OP_MOV_AX_Ov, 0xcd, 0xab, // mov ax,[0xabcd]
        OP_MOV_Ob_AL, 0xcd, 0xab, // mov [0xabcd],al
        OP_MOV_Ov_AX, 0xab, 0xcd, // mov [0xcdab],ax
        OP_MOV_AL_Ib, 0x66, // mov al,0x66
        OP_MOV_CL_Ib, 0x66, // mov cl,0x66
        OP_MOV_DL_Ib, 0x66, // mov dl,0x66
        OP_MOV_BL_Ib, 0x66, // mov bl,0x66
        OP_MOV_AH_Ib, 0x66, // mov ah,0x66
        OP_MOV_CH_Ib, 0x66, // mov ch,0x66
        OP_MOV_DH_Ib, 0x66, // mov dh,0x66
        OP_MOV_BH_Ib, 0x66, // mov bh,0x66
        OP_MOV_AX_Iv, 
        OP_MOV_CX_Iv, 
        OP_MOV_DX_Iv, 
        OP_MOV_BX_Iv, 
        OP_MOV_SP_Iv, 
        OP_MOV_BP_Iv, 
        OP_MOV_SI_Iv, 
        OP_MOV_DI_Iv, 
        OP_MOV_Eb_Ib, 
        OP_MOV_Ev_Iv,         
    };
    setupCode(code, sizeof(code));

    reg8(REG_AH) = 69;
    reg16(REG_BP) = 123;
    reg16(REG_DI) = 456;
    reg16(REG_SS) = 789;
    off = SEG_OFFSET(789) + 123 + 456;
    cpu_->step(); // mov [bp+di],ah
    ASSERT_EQ(mem_->readByte(off), 69);

    reg16(REG_BX) = 639;
    reg16(REG_SI) = 157;
    reg16(REG_DS) = 371;
    off = SEG_OFFSET(371) + 639 + 157 + 0xa;
    mem_->writeByte(off, 201);
    cpu_->step(); // mov cl,[bx+si+0xa]
    ASSERT_EQ(reg8(REG_CL), 201);

    reg16(REG_DX) = 4265;
    off = SEG_OFFSET(371) + 157 + 0xa8c;
    cpu_->step(); // mov [si+0xa8c],dx
    ASSERT_EQ(mem_->readWord(off), 4265);

    reg16(REG_CX) = 7488;
    cpu_->step(); // mov si,cx
    ASSERT_EQ(reg16(REG_SI), 7488);

    reg16(REG_ES) = 6548;
    off = SEG_OFFSET(371) + 639;
    cpu_->step(); // mov [bx],es
    ASSERT_EQ(mem_->readWord(off), 6548);

    off = SEG_OFFSET(6548) + 456 + 0x8c;
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
}