#ifndef REGISTERS_H
#define REGISTERS_H

#include <string>
#include "dos/types.h"
#include "dos/address.h"

// TODO: this is stupid, get rid of non-unique register codes
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

// TODO: this is not portable
class Registers {
private:
    union {
        Word bit16[REG_FLAGS + 1]; // 14x 16bit registers
        Byte bit8[REG_DL + 1];     // 8x 8bit registers
    } regs_;

public:
    Registers();
    inline Byte& bit8(const Register reg) { return regs_.bit8[reg]; }
    inline Word& bit16(const Register reg) { return regs_.bit16[reg]; }
    inline const Byte& bit8(const Register reg) const { return regs_.bit8[reg]; }
    inline const Word& bit16(const Register reg) const { return regs_.bit16[reg]; }
    inline bool getFlag(const Flag flag) const { return regs_.bit16[REG_FLAGS] & flag; }
    inline void setFlag(const Flag flag, const bool val) { 
        if (val) regs_.bit16[REG_FLAGS] |= flag; 
        else regs_.bit16[REG_FLAGS] &= ~flag; 
    }
    inline Address csip() const { return { regs_.bit16[REG_CS], regs_.bit16[REG_IP]}; }
    std::string dump() const;
    void reset();
};

#endif // REGISTERS_H
