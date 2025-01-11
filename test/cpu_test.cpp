#include <vector>
#include <limits>
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "debug.h"
#include "dos/memory.h"
#include "dos/cpu.h"
#include "dos/opcodes.h"
#include "dos/modrm.h"
#include "dos/util.h"
#include "dos/interrupt.h"
#include "dos/instruction.h"

using namespace std;
using ::testing::_;
using ::testing::Return;

class MockInterruptHandler : public InterruptInterface {
public:
    MOCK_METHOD(IntStatus, interrupt, (const Byte num, Registers &regs), (override));
};

class CpuTest : public ::testing::Test {
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

    inline Word getReg(const Register reg) { return regs_->get(reg); }
    inline void setReg(const Register reg, const Word value) { regs_->set(reg, value); }
    inline bool flag(const Flag flag) { return regs_->getFlag(flag); }
    inline Word instructionLength() const { return cpu_->instructionLength(); }
};

TEST_F(CpuTest, OrderHL) {
    setReg(REG_AX, 0x4c7b);
    setReg(REG_BX, 0x1234);
    setReg(REG_CX, 0x0b0c);
    setReg(REG_DX, 0xfefc);
    TRACELN(regs_->dump());
    ASSERT_EQ(getReg(REG_AH), 0x4c);
    ASSERT_EQ(getReg(REG_AL), 0x7b);
    ASSERT_EQ(getReg(REG_BH), 0x12);
    ASSERT_EQ(getReg(REG_BL), 0x34);
    ASSERT_EQ(getReg(REG_CH), 0x0b);
    ASSERT_EQ(getReg(REG_CL), 0x0c);
    ASSERT_EQ(getReg(REG_DH), 0xfe);
    ASSERT_EQ(getReg(REG_DL), 0xfc);

    const Word val = 0x1234;
    ASSERT_EQ(lowByte(val), 0x34);
    ASSERT_EQ(hiByte(val), 0x12);
}

TEST_F(CpuTest, Flags) {
    auto flagset = vector<Flag>{ 
        FLAG_CARRY, FLAG_B1, FLAG_PARITY, FLAG_B3, FLAG_AUXC, FLAG_B5, FLAG_ZERO, FLAG_SIGN, 
        FLAG_TRAP, FLAG_INT, FLAG_DIR, FLAG_OVER, FLAG_B12, FLAG_B13, FLAG_B14, FLAG_B15
    };
    for (auto flag : flagset) {
        regs_->setFlag(flag, true);
        ASSERT_TRUE(regs_->getFlag(flag));
    }
    ASSERT_EQ(getReg(REG_FLAGS), 0xffff);
    for (auto flag : flagset) {
        regs_->setFlag(flag, false);
        ASSERT_FALSE(regs_->getFlag(flag));
    }    
    ASSERT_EQ(getReg(REG_FLAGS), 0x0);
    for (auto flag : flagset) {
        regs_->setFlag(flag, true);
        ASSERT_TRUE(regs_->getFlag(flag));
        regs_->setFlag(flag, false);
        ASSERT_FALSE(regs_->getFlag(flag));
    }
    ASSERT_EQ(getReg(REG_FLAGS), 0x0);
}

TEST_F(CpuTest, DISABLED_Arithmetic) {
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
    ASSERT_EQ(getReg(REG_IP), 0);
    cpu_->step(); // mov al, 0xff
    ASSERT_EQ(getReg(REG_IP), 2);
    ASSERT_EQ(getReg(REG_AL), 0xff);
    cpu_->step(); // add al, 0x1
    ASSERT_EQ(getReg(REG_IP), 4);
    ASSERT_EQ(getReg(REG_AL), 0x00);
    ASSERT_EQ(getReg(REG_FLAGS) & mask, 0b1010101); // C1 Z1 S0 O0 A1 P1
    cpu_->step();
    ASSERT_EQ(getReg(REG_IP), 6);
    ASSERT_EQ(getReg(REG_AL), 0x02);
    cpu_->step();
    ASSERT_EQ(getReg(REG_IP), 8);
    ASSERT_EQ(getReg(REG_AL), 0x05);
    ASSERT_EQ(getReg(REG_FLAGS) & mask, 0b100); // C0 Z0 S0 O0 A0 P1
    cpu_->step(); // mov al,120
    ASSERT_EQ(getReg(REG_IP), 10);
    ASSERT_EQ(getReg(REG_AL), 120);
    cpu_->step(); // add al,10
    ASSERT_EQ(getReg(REG_IP), 12);
    ASSERT_EQ(getReg(REG_AL), 130);
    ASSERT_EQ(getReg(REG_FLAGS) & mask, 0b100010010100); // C0 Z0 S1 O1 A1 P1
    cpu_->step(); // mov al,0x0
    ASSERT_EQ(getReg(REG_IP), 14);
    ASSERT_EQ(getReg(REG_AL), 0x0);
    cpu_->step(); // sub al,0x1
    ASSERT_EQ(getReg(REG_IP), 16);
    ASSERT_EQ(getReg(REG_AL), 0xff);
    ASSERT_EQ(getReg(REG_FLAGS) & mask, 0b10010101); // C1 Z0 S1 O0 A1 P1
}

TEST_F(CpuTest, DISABLED_WordOperand) {
    const Byte code[] = {
        0xbf, 0x6f, 0x01 // mov di,0x16f
    };
    setupCode(code, sizeof(code));
    cpu_->step();
    ASSERT_EQ(getReg(REG_DI), 0x16f);
}

// test all combinations of the mov opcodes with modrm modificator bytes
TEST_F(CpuTest, DISABLED_Mov) {
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

    setReg(REG_AH, 69);
    setReg(REG_BP, 123);
    setReg(REG_DI, 456);
    setReg(REG_SS, 789);
    off = SEG_TO_OFFSET(getReg(REG_SS)) + getReg(REG_BP) + getReg(REG_DI);
    cpu_->step(); // mov [bp+di],ah
    ASSERT_EQ(mem_->readByte(off), getReg(REG_AH));

    setReg(REG_BX, 639);
    setReg(REG_SI, 157);
    setReg(REG_DS, 371);
    off = SEG_TO_OFFSET(getReg(REG_DS)) + getReg(REG_BX) + getReg(REG_SI) + 0xa;
    mem_->writeByte(off, 201);
    cpu_->step(); // mov cl,[bx+si+0xa]
    ASSERT_EQ(getReg(REG_CL), 201);

    setReg(REG_DX, 4265);
    off = SEG_TO_OFFSET(getReg(REG_DS)) + getReg(REG_SI) + 0xa8c;
    cpu_->step(); // mov [si+0xa8c],dx
    ASSERT_EQ(mem_->readWord(off), getReg(REG_DX));

    setReg(REG_CX, 7488);
    cpu_->step(); // mov si,cx
    ASSERT_EQ(getReg(REG_SI), getReg(REG_CX));

    setReg(REG_ES, 6548);
    off = SEG_TO_OFFSET(getReg(REG_DS)) + getReg(REG_BX);
    cpu_->step(); // mov [bx],es
    ASSERT_EQ(mem_->readWord(off), getReg(REG_ES));

    off = SEG_TO_OFFSET(getReg(REG_ES)) + getReg(REG_DI) - 0x74;
    mem_->writeWord(off, 0xfafe);
    cpu_->step(); // mov ss,[di+0x8c]
    ASSERT_EQ(getReg(REG_SS), 0xfafe);

    off = SEG_TO_OFFSET(getReg(REG_DS)) + 0xcdab;
    mem_->writeByte(off, 0x64);
    cpu_->step(); // mov al,[0xcdab]
    ASSERT_EQ(getReg(REG_AL), 0x64);

    off = SEG_TO_OFFSET(getReg(REG_DS)) + 0xabcd;
    mem_->writeWord(off, 0xcaca);
    cpu_->step(); // mov ax,[0xabcd]
    ASSERT_EQ(getReg(REG_AX), 0xcaca);

    off = SEG_TO_OFFSET(getReg(REG_DS)) + 0xabcd;
    cpu_->step(); // mov [0xabcd],al
    ASSERT_EQ(mem_->readByte(off), getReg(REG_AL));

    off = SEG_TO_OFFSET(getReg(REG_DS)) + 0xcdab;
    cpu_->step(); // mov [0xcdab],ax
    ASSERT_EQ(mem_->readWord(off), getReg(REG_AX));

    cpu_->step(); // mov al,0x66
    ASSERT_EQ(getReg(REG_AL), 0x66);
    cpu_->step(); // mov cl,0x67
    ASSERT_EQ(getReg(REG_CL), 0x67);
    cpu_->step(); // mov dl,0x68
    ASSERT_EQ(getReg(REG_DL), 0x68);
    cpu_->step(); // mov bl,0x69
    ASSERT_EQ(getReg(REG_BL), 0x69);
    cpu_->step(); // mov ah,0x6a
    ASSERT_EQ(getReg(REG_AH), 0x6a);
    cpu_->step(); // mov ch,0x6b
    ASSERT_EQ(getReg(REG_CH), 0x6b);
    cpu_->step(); // mov dh,0x6c
    ASSERT_EQ(getReg(REG_DH), 0x6c);
    cpu_->step(); // mov bh,0x6d
    ASSERT_EQ(getReg(REG_BH), 0x6d);

    cpu_->step(); // mov ax,0x1234
    ASSERT_EQ(getReg(REG_AX), 0x1234);
    cpu_->step(); // mov cx,0x1235
    ASSERT_EQ(getReg(REG_CX), 0x1235);
    cpu_->step(); // mov dx,0x1236
    ASSERT_EQ(getReg(REG_DX), 0x1236);
    cpu_->step(); // mov bx,0x1237
    ASSERT_EQ(getReg(REG_BX), 0x1237);
    cpu_->step(); // mov sp,0x1238
    ASSERT_EQ(getReg(REG_SP), 0x1238);
    cpu_->step(); // mov bp,0x1239
    ASSERT_EQ(getReg(REG_BP), 0x1239);
    cpu_->step(); // mov si,0x123a
    ASSERT_EQ(getReg(REG_SI), 0x123a);
    cpu_->step(); // mov di,0x123b
    ASSERT_EQ(getReg(REG_DI), 0x123b);

    off = SEG_TO_OFFSET(getReg(REG_DS)) + getReg(REG_BX) + getReg(REG_DI) + 0x1024;
    cpu_->step(); // mov [bx+di+0x1024],0xab
    ASSERT_EQ(mem_->readByte(off), 0xab);

    off = SEG_TO_OFFSET(getReg(REG_DS)) + 0xbabe;
    cpu_->step(); // mov [0xbabe],0xabcd
    ASSERT_EQ(mem_->readWord(off), 0xabcd);

    // negative displacement value
    off = SEG_TO_OFFSET(getReg(REG_DS)) + getReg(REG_BX) + getReg(REG_DI) - 0x101;
    cpu_->step(); // mov [bx+di-0x101], 0xab
    ASSERT_EQ(mem_->readByte(off), 0xab);

    // segment override prefix
    off = SEG_TO_OFFSET(getReg(REG_ES)) + getReg(REG_BX) + getReg(REG_DI);
    cpu_->step(); // mov [es:bx+di],ch
    ASSERT_EQ(mem_->readByte(off), getReg(REG_CH));
}

TEST_F(CpuTest, DISABLED_Int) {
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

TEST_F(CpuTest, DISABLED_CmpSub) {
    Byte code[] = {
        OP_SUB_Gv_Ev, MODRM_MOD_REG | MODRM_REG_AX | MODRM_REG_BX >> MODRM_REG_SHIFT,  // sub, ax,bx
    };
    SWord result[] = { 123-3, 123+3, -123-3, -123+3, 123, 123,-123,-123 };

    for (int i = 0; i < 2; ++i) {
        setupCode(code, sizeof(code));
        setReg(REG_AX, 123);
        setReg(REG_BX, 3);
        cpu_->step();
        ASSERT_EQ(getReg(REG_AX), result[0+i*4]);
        ASSERT_FALSE(flag(FLAG_CARRY));
        ASSERT_FALSE(flag(FLAG_ZERO));
        ASSERT_FALSE(flag(FLAG_SIGN));
        ASSERT_FALSE(flag(FLAG_OVER));
        ASSERT_FALSE(flag(FLAG_AUXC));
        ASSERT_TRUE(flag(FLAG_PARITY));

        setupCode(code, sizeof(code));
        setReg(REG_AX, 123);
        setReg(REG_BX, WORD_SIGNED(-3));
        cpu_->step();
        ASSERT_EQ(getReg(REG_AX), result[1+i*4]);
        ASSERT_TRUE(flag(FLAG_CARRY));
        ASSERT_FALSE(flag(FLAG_ZERO));
        ASSERT_FALSE(flag(FLAG_SIGN));
        ASSERT_FALSE(flag(FLAG_OVER));
        ASSERT_TRUE(flag(FLAG_AUXC));
        ASSERT_TRUE(flag(FLAG_PARITY));

        setupCode(code, sizeof(code));
        setReg(REG_AX, WORD_SIGNED(-123));
        setReg(REG_BX, 3);
        cpu_->step();
        ASSERT_EQ(WORD_SIGNED(getReg(REG_AX)), result[2+i*4]);
        ASSERT_FALSE(flag(FLAG_CARRY));
        ASSERT_FALSE(flag(FLAG_ZERO));
        ASSERT_TRUE(flag(FLAG_SIGN));
        ASSERT_FALSE(flag(FLAG_OVER));
        ASSERT_FALSE(flag(FLAG_AUXC));
        ASSERT_TRUE(flag(FLAG_PARITY));

        setupCode(code, sizeof(code));
        setReg(REG_AX, WORD_SIGNED(-123));
        setReg(REG_BX, WORD_SIGNED(-3));
        cpu_->step();
        ASSERT_EQ(WORD_SIGNED(getReg(REG_AX)), result[3+i*4]);
        ASSERT_TRUE(flag(FLAG_CARRY));
        ASSERT_FALSE(flag(FLAG_ZERO));
        ASSERT_TRUE(flag(FLAG_SIGN));
        ASSERT_FALSE(flag(FLAG_OVER));
        ASSERT_TRUE(flag(FLAG_AUXC));
        ASSERT_TRUE(flag(FLAG_PARITY));

        code[0] = OP_CMP_Gv_Ev;
    }

}

TEST_F(CpuTest, Instruction) {
    const Byte code[] = {
        0x06, // push es
        0x49, // dec cx
        0xb9, 0x34, 0x12, // mov cx,0x1234
        0xf3, 0xaa, // rep stosb
        0x81, 0x78, 0x10, 0xcd, 0xab, // cmp word [bx+si+0x10],0xabcd
        0x36, 0x21, 0x16, 0xac, 0x19, // and [ss:0x19ac],dx
        0x74, 0xfd, // jz -0x3 (+0x11+0x2 = 0x10)
        0xd0, 0xc0, // rol al,1
        0x36, 0xA3, 0x52, 0x00, // mov [ss:0x52],ax
        0xF6, 0xC2, 0x80, // test dl,0x80
        0xff, 0x18, // call far [bx+si]
        0xd0, 0x0a, // ror byte [bp+si],1
        0x0B, 0xA4, 0x0B, 0x83, // or sp,[si-0x7cf5]
        0xff, 0xe1, // jmp cx
        0x26, 0x8e, 0x06, 0x2c, 0x00, // mov es, es:[0x2c]
        0xa2, 0xce, 0x00, // mov [0xce],al
    };
    const std::string instructions[] = {
        "push es",
        "dec cx",
        "mov cx, 0x1234",
        "repz stosb",
        "cmp word [bx+si+0x10], 0xabcd",
        "and ss:[0x19ac], dx",
        "jz 0x10",
        "rol al, 1",
        "mov ss:[0x52], ax",
        "test dl, 0x80",
        "call far [bx+si]",
        "ror byte [bp+si], 1",
        "or sp, [si-0x7cf5]",
        "jmp cx",
        "mov es, es:[0x2c]",
        "mov [0xce], al",
    };
    const std::string patterns[] = {
        "06", // push es
        "49", // dec cx
        "b9????", // mov cx,0x1234
        "f3aa", // rep stosb
        "8178??????", // cmp word [bx+si+0x10],0xabcd
        "362116????", // and [ss:0x19ac],dx
        "74??", // jz -0x3 (+0x11+0x2 = 0x10)
        "d0c0", // rol al,1
        "36a3????", // mov [ss:0x52],ax
        "f6c2??", // test dl,0x80
        "ff18", // call far [bx+si]
        "d00a", // ror byte [bp+si],1
        "0ba4????", // or sp,[si-0x7cf5]
        "ffe1", // jmp cx
        "268e06????", // mov es, es:[0x2c]
        "a2????", // mov [0xce],al
    };
    const DWord signatures[] = {
        //PRF|CLASS  |OP1TYP|OP2TYP|
        0b000'0000010'010100'000001, // none|push|reg_es|none
        0b000'0010000'001000'000001, // none|dec|reg_cx||none
        0b000'0010110'001000'110010, // none|mov|reg_cx|imm16
        0b110'0100110'000001'000001, // rep|stosb|none|none
        0b000'0001101'100111'110010, // none|cmp|mem_bx_si_off16|imm16
        0b011'0000111'100110'001011, // ss|and|mem_off16|reg_dx
        0b000'0010010'110010'000001, // none|jmp_if|imm_8|none
        0b000'1000111'000011'110000, // none|rol|reg_al|imm_1
        0b011'0010110'100110'000010, // ss|mov|mem_off16|reg_ax
        0b000'0010100'001100'110010, // none|test|reg_dl|imm_8
        0b000'0011100'010110'000001, // none|call_far|mem_bx_si|none
        0b000'1001000'011000'110000, // none|ror|mem_bp_si|imm_1
        0b000'0000100'010001'101011, // none|or|reg_sp|mem_si_off16
        0b000'0010001'001000'000001, // none|jmp|reg_cx|none
        0b001'0010110'010100'100110, // es|mov|reg_es|mem_off16
        0b000'0010110'100110'000011, // none|mov|mem_off16|reg_al
    };
    const Size lengths[] = {
        1, 1, 3, 2, 5, 5, 2, 2, 4, 3, 2, 2, 4, 2, 5, 3,
    };
    const int icount = sizeof(lengths) / sizeof(Size);

    Size codeofs = 0;
    for (int i = 0; i < icount; ++i) {
        Instruction ins(Address(0, codeofs), code + codeofs);
        TRACELN("--- " << hexVal(ins.addr.toLinear()) << ": instruction " << i + 1 << ": '" << ins.toString() 
            << "' (length = " << static_cast<int>(ins.length) << ")" << ", memOfs = " << ins.memOffset() << "/" << hexVal(ins.memOffset()));
        ASSERT_EQ(ins.toString(), instructions[i]);
        ASSERT_EQ(ins.length, lengths[i]);
        const string patStr = numericToHexa(ins.pattern());
        TRACELN("pattern: " << patStr << ", expected: " << patterns[i]);
        const DWord sig = ins.signature();
        ASSERT_EQ(patStr, patterns[i]);
        TRACELN("signature: " << hexVal(sig) << "/" << binString(sig) << endl
             << "expected:  " << hexVal(signatures[i]) << "/" << binString(signatures[i]));
        ASSERT_EQ(sig, signatures[i]);
        codeofs += ins.length;
    }
}

TEST_F(CpuTest, FarCall) {
    const Byte code[] = { 0x9a, 0x2f, 0xc, 0xb5, 0x6 }; // call 0x6b5:0xc2f
    Instruction ins(Address{0, 0}, code);
    TRACELN("instrucion: " << ins.toString());
    ASSERT_TRUE(ins.isFarCall());
    const Address a = ins.op1.farAddr();
    ASSERT_EQ(a.segment, 0x6b5);
    ASSERT_EQ(a.offset, 0xc2f);
}

TEST_F(CpuTest, InstructionMatch) {
    const Address a{0x1000, 0x0};
    const Byte code[] = {
        0xEB, 0x0B, // jmp short 0xb
        0x75, 0x0B, // jnz 0x387
        0x80, 0x7e, 0xf6, 0x00, // cmp byte [bp-0xa], 0
        0x83, 0x7e, 0xf6, 0x00, // cmp word [bp-0x2], 0
    };
    Instruction i1{a, code}, i2{a, code + 2},
        i3{a, code + 4}, i4{a, code + 8};
    ASSERT_EQ(i1.match(i2), INS_MATCH_MISMATCH);
    ASSERT_EQ(i2.match(i1), INS_MATCH_MISMATCH);
    ASSERT_EQ(i3.match(i4), INS_MATCH_MISMATCH);
}

TEST_F(CpuTest, BranchOffset) {
    const Word base = 0x6b00;
    const Address a{0x1000, base};
    const Byte code[] = {
        0x75, 0x22, // jnz +34
        0x75, 0xde, // jnz -34
        0xe8, 0xbc, 0x6a, // call +27324
        0xe8, 0x44, 0x95, // call -27324
    };
    Instruction i1{a, code}, i2{a, code + i1.length};
    ASSERT_EQ(i1.relativeOffset(), 34);
    ASSERT_EQ(i1.absoluteOffset(), base + 2 + 0x22);
    ASSERT_EQ(i1.toString(true), "jnz 0x6b24 (0x22 down)");
    ASSERT_EQ(i2.relativeOffset(), -34);
    ASSERT_EQ(i2.absoluteOffset(), base + 2 - 0x22);
    ASSERT_EQ(i2.toString(true), "jnz 0x6ae0 (0x22 up)");
    Instruction i3{a, code + 4}, i4{a, code + 7};
    ASSERT_EQ(i3.relativeOffset(), 27324);
    ASSERT_EQ(i3.absoluteOffset(), base + 3 + 27324);
    ASSERT_EQ(i3.toString(true), "call 0xd5bf (0x6abc down)");
    ASSERT_EQ(i4.relativeOffset(), -27324);
    ASSERT_EQ(i4.absoluteOffset(), base + 3 - 27324);
    ASSERT_EQ(i4.toString(true), "call 0x47 (0x6abc up)");
}