#ifndef CPU_H
#define CPU_H

#include <string>
#include "types.h"
#include "memory.h"

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
    friend class InterruptHandler;

public:
    virtual std::string type() const = 0;
    virtual std::string info() const = 0;
    virtual void init(const SegmentedAddress &code, const SegmentedAddress &stack) = 0;
    virtual void step() = 0;
    virtual void run() = 0;

protected:
    virtual Byte& reg8(const Register reg) = 0;
    virtual Word& reg16(const Register reg) = 0;
    virtual const Byte& reg8(const Register reg) const = 0;
    virtual const Word& reg16(const Register reg) const = 0;
    virtual bool getFlag(const Flag flag) const = 0;
    virtual void setFlag(const Flag flag, const bool val) = 0;
};

class InterruptInterface;

class Cpu_8086 : public Cpu {
    friend class InterruptHandler;
    friend class Cpu_8086_Test;

private:
    Byte *memBase_, *code_;
    InterruptInterface *int_;
    union {
        Word reg16[REG_FLAGS + 1]; // 14x 16bit registers
        Byte reg8[REG_DL + 1];     // 8x 8bit registers
    } regs_;
    Byte opcode_, byte1_, byte2_, resultb_;
    Word word1_, word2_, resultw_;
    bool done_, step_;

public:
    Cpu_8086(Byte *memBase, InterruptInterface *intif);
    std::string type() const override { return "8086"; };
    std::string info() const override;
    void init(const SegmentedAddress &codeAddr, const SegmentedAddress &stackAddr) override;
    void step() override;
    void run() override;

private:
    inline Byte& reg8(const Register reg) override { return regs_.reg8[reg]; }
    inline Word& reg16(const Register reg) override { return regs_.reg16[reg]; }
    inline const Byte& reg8(const Register reg) const { return regs_.reg8[reg]; }
    inline const Word& reg16(const Register reg) const { return regs_.reg16[reg]; }
    inline bool getFlag(const Flag flag) const override { return regs_.reg16[REG_FLAGS] & flag; }
    inline void setFlag(const Flag flag, const bool val) override { 
        if (val) regs_.reg16[REG_FLAGS] |= flag; 
        else regs_.reg16[REG_FLAGS] &= ~flag; 
    }
    inline Byte ipByte(const Word offset = 0) { return code_[regs_.reg16[REG_IP] + offset]; }
    inline void ipAdvance(const Word amount) { regs_.reg16[REG_IP] += amount; }
    std::string flagString(const Word flags) const;
    void resetRegs();

    void pipeline();
    void fetch();
    void evaluate();
    void commit();
    void advance();
    void unknown(const std::string &stage);
    
    void instr_add(const Byte opcode);
    void instr_sub(const Byte opcode);
    void instr_cmp(const Byte opcode);
    void instr_mov(const Byte opcode);
    void instr_int();

};


// stc: 0x3103 0011000100000011
// clc: 0x3102 0011000100000010

#endif // CPU_H
