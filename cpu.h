#ifndef CPU_H
#define CPU_H

#include <string>
#include "types.h"
#include "memory.h"

enum RegId {
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
};

enum Flag {
    FLAG_CF  = 0, // carry
    FLAG_B1  = 1, // ----
    FLAG_PF  = 2, // parity
    FLAG_B3  = 3, // ----
    FLAG_AF  = 4, // auxiliary carry
    FLAG_B5  = 5, // ----
    FLAG_ZF  = 6, // zero
    FLAG_SF  = 7, // sign
    FLAG_TF  = 8, // trap
    FLAG_IF  = 9, // interrupt
    FLAG_DF  = 10, // direction
    FLAG_OF  = 11, // overflow
    FLAG_B12 = 12, // ----
    FLAG_B13 = 13, // ----
    FLAG_B14 = 14, // ----
    FLAG_B15 = 15, // ----
};

class Dos;

class Cpu {
protected:
    Byte* memBase_;
    Dos* os_;

public:
    Cpu() : memBase_(nullptr), os_(nullptr) {}
    virtual void setMemBase(Byte* memBase) { memBase_ = memBase; }
    virtual void setOs(Dos *os) { os_ = os; }

    virtual std::string type() const = 0;
    virtual std::string info() const = 0;
    virtual const Byte& reg8(const RegId reg) const = 0;
    virtual const Word& reg16(const RegId reg) const = 0;
    virtual Byte& reg8(const RegId reg) = 0;
    virtual Word& reg16(const RegId reg) = 0;
    virtual bool getFlag(const Flag flag) const = 0;
    virtual void setFlag(const Flag flag, const bool val) = 0;

    virtual void reset(const SegmentedAddress &code, const SegmentedAddress &stack, const Word &pspSeg) = 0;
    virtual void exec() = 0;
};

class Cpu_8086 : public Cpu {
private:
    union {
        Word reg16[REG_FLAGS + 1]; // 14x 16bit registers
        Byte reg8[REG_DL + 1];     // 8x 8bit registers
    } regs_;
    bool done_;

public:
    Cpu_8086();
    virtual std::string type() const override { return "8086"; };
    virtual std::string info() const;
    inline virtual const Byte& reg8(const RegId reg) const { return regs_.reg8[reg]; }
    inline virtual const Word& reg16(const RegId reg) const { return regs_.reg16[reg]; }
    inline virtual Byte& reg8(const RegId reg) { return regs_.reg8[reg]; }
    inline virtual Word& reg16(const RegId reg) { return regs_.reg16[reg]; }
    inline virtual bool getFlag(const Flag flag) const { return (regs_.reg16[REG_FLAGS] >> flag) & 1; }
    inline virtual void setFlag(const Flag flag, const bool val) { 
        if (val) regs_.reg16[REG_FLAGS] |= 1 << flag; 
        else regs_.reg16[REG_FLAGS] &= ~(1 << flag); 
    }

    virtual void reset(const SegmentedAddress &code, const SegmentedAddress &stack, const Word &pspSeg);
    virtual void exec();

private:
    void setupRegs();
    void mov_byte(const Byte dst, const Byte val);
    void interrupt(const Byte val);
};


// stc: 0x3103 0011000100000011
// clc: 0x3102 0011000100000010

#endif // CPU_H
