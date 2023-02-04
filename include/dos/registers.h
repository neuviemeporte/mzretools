#ifndef REGISTERS_H
#define REGISTERS_H

#include <string>
#include "dos/types.h"
#include "dos/address.h"

enum Register { 
    REG_NONE, 
    REG_AL, REG_AH, REG_BL, REG_BH, REG_CL, REG_CH, REG_DL,REG_DH,
    REG_AX, REG_BX, REG_CX, REG_DX,
    REG_SI, REG_DI, REG_BP, REG_SP,
    REG_CS, REG_DS, REG_ES, REG_SS,
    REG_IP, REG_FLAGS,
};

inline bool regIsByte(const Register reg) { return reg >= REG_AL && reg <= REG_DH; }
inline bool regIsWord(const Register reg) { return reg >= REG_AX; }
inline bool regIsGeneral(const Register reg) { return reg >= REG_AX && reg <= REG_DX; }
inline bool regIsSegment(const Register reg) { return reg >= REG_CS && reg <= REG_SS; }
std::string regName(const Register r);
Register regHigh(const Register r);
Register regLow(const Register r);

// flag word bits: XXXXODITSZXAXPXC
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

class Registers {
private:
    Word values[REG_FLAGS - REG_AX + 1]; // 14x 16bit registers

public:
    Registers();
    Word get(const Register r) const;
    Byte getHigh(const Register r) const;
    Byte getLow(const Register r) const;
    void set(const Register r, const Word value);
    inline bool getFlag(const Flag flag) const { return reg(REG_FLAGS) & flag; }
    inline void setFlag(const Flag flag, const bool val) { 
        if (val) reg(REG_FLAGS) |= flag; 
        else reg(REG_FLAGS) &= ~flag; 
    }
    inline Address csip() const { return { values[REG_CS], values[REG_IP]}; }
    std::string dump() const;
    void reset();
private:
    inline Word& reg(const Register r) { return values[r - REG_AX]; }
    inline const Word& reg(const Register r) const { return values[r - REG_AX]; }
};

#endif // REGISTERS_H
