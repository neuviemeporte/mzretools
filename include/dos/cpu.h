#ifndef CPU_H
#define CPU_H

#include <string>
#include "dos/types.h"
#include "dos/memory.h"

enum Register {
    REG_AX = 0, REG_AL = 0, REG_AH = 1,
    REG_BX = 1, REG_BL = 2, REG_BH = 3,
    REG_CX = 2, REG_CL = 4, REG_CH = 5,
    REG_DX = 3, REG_DL = 6, REG_DH = 7,
    REG_SI = 4, 
    REG_DI = 5, 
    REG_BP = 6, 
    REG_SP = 7,
    REG_CS = 8, 
    REG_DS = 9, 
    REG_ES = 10,
    REG_SS = 11, 
    REG_IP = 12, 
    REG_FLAGS = 13,
    REG_NONE
};

enum RegType {
    REG_GP8,  // general purpose 8bit
    REG_GP16, // general purpose 16bit
    REG_SEG   // segment register
};

// XXXXODITSZXAXPXC
   //1111001000000000
enum Flag : Word {
    FLAG_CARRY  = 0b0000000000000001, // carry
    FLAG_B1     = 0b0000000000000010, // ----
    FLAG_PARITY = 0b0000000000000100, // parity
    FLAG_B3     = 0b0000000000001000, // ----
    FLAG_AUXC   = 0b0000000000010000, // auxiliary carry
    FLAG_B5     = 0b0000000000100000, // ----
    FLAG_ZERO   = 0b0000000001000000, // zero
    FLAG_SIGN   = 0b0000000010000000, // sign
    FLAG_TRAP   = 0b0000000100000000, // trap
    FLAG_INT    = 0b0000001000000000, // interrupt
    FLAG_DIR    = 0b0000010000000000, // direction
    FLAG_OVER   = 0b0000100000000000, // overflow
    FLAG_B12    = 0b0001000000000000, // ----
    FLAG_B13    = 0b0010000000000000, // ----
    FLAG_B14    = 0b0100000000000000, // ----
    FLAG_B15    = 0b1000000000000000, // ----
};   

class Cpu {
public:
    virtual std::string type() const = 0;
    virtual std::string info() const = 0;
    virtual void init(const Address &code, const Address &stack) = 0;
    virtual void step() = 0;
    virtual void run() = 0;
    // raw register access
    virtual Byte* regs8() = 0;
    virtual Word* regs16() = 0;
};

class InterruptInterface;

class Cpu_8086 : public Cpu {
private:
    Memory *mem_;
    InterruptInterface *int_;
    union {
        Word reg16[REG_FLAGS + 1]; // 14x 16bit registers
        Byte reg8[REG_DL + 1];     // 8x 8bit registers
    } regs_;
    const Byte *memBase_, *code_;
    Byte opcode_, modrm_mod_, modrm_reg_, modrm_mem_;
    Byte byteOperand1_, byteOperand2_, byteResult_;
    Word wordOperand1_, wordOperand2_, wordResult_;
    bool done_, step_;

public:
    Cpu_8086(Memory *memory, InterruptInterface *inthandler);
    std::string type() const override { return "8086"; };
    std::string info() const override;
    void init(const Address &codeAddr, const Address &stackAddr) override;
    void step() override;
    void run() override;
    Byte* regs8() override { return regs_.reg8; }
    Word* regs16() override { return regs_.reg16; }

private:
    // utility
    void resetRegs();
    std::string flagString(const Word flags) const;
    Register defaultSeg(const Register) const;

    // convenient register access
    inline Byte& reg8(const Register reg) { return regs_.reg8[reg]; }
    inline Word& reg16(const Register reg) { return regs_.reg16[reg]; }
    inline const Byte& reg8(const Register reg) const { return regs_.reg8[reg]; }
    inline const Word& reg16(const Register reg) const { return regs_.reg16[reg]; }
    inline bool getFlag(const Flag flag) const { return regs_.reg16[REG_FLAGS] & flag; }
    inline void setFlag(const Flag flag, const bool val) { 
        if (val) regs_.reg16[REG_FLAGS] |= flag; 
        else regs_.reg16[REG_FLAGS] &= ~flag; 
    }

    // instruction pointer access and manipulation
    inline Byte ipByte(const Word offset = 0) const { return code_[reg16(REG_IP) + offset]; }
    inline Word ipWord(const Word offset = 0) const { return *reinterpret_cast<const Word*>(code_ + reg16(REG_IP) + offset); }
    inline void ipAdvance(const Word amount) { regs_.reg16[REG_IP] += amount; }
    inline Address csip() const { return {reg16(REG_CS), reg16(REG_IP)}; }

    // ModR/M byte parsing and evaluation
    inline Byte modrm_mod(const Byte modrm) const { return modrm >> 6; }
    inline Byte modrm_reg(const Byte modrm) const { return (modrm >> 3) & 0b111; }
    inline Byte modrm_mem(const Byte modrm) const { return modrm & 0b111; }
    void modrmParse();
    Register modrmRegister(const RegType type, const Byte value) const;
    inline Register modrmRegRegister(const RegType regType) const;
    inline Register modrmMemRegister(const RegType regType) const;
    Offset modrmMemAddress() const;
    Word modrmInstructionLength() const;

    // instruction execution pipeline
    void pipeline();
    void dispatch();
    void unknown(const std::string &stage);

    // instructions 
    void instr_mov();
};

#endif // CPU_H
