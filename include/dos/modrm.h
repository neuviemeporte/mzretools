#ifndef MODRM_H
#define MODRM_H

#include "dos/types.h"

// ModR/M byte semantics: it follows some instructions and has the following bits: AABBBCCC
// AA: MOD field:
//    00: interpret CCC as Table1 to calculate address of memory operand
//    01: interpret CCC as Table2 with 8bit signed displacement to calculate address of memory operand
//    10: interpret CCC as Table2 with 16bit signed displacement to calculate address of memory operand
//    11: interpret CCC as REG to use register as operand
// BBB: REG field:
//    8bit instruction:  000: AL     001: CL     010: DL     011: BL     100: AH     101: CH     110: DH     111: BH
//    16bit instruction: 000: AX     001: CX     010: DX     011: BX     100: SP     101: BP     110: SI     111: DI
//    segment register:  000: ES     001: CS     010: SS     011: DS
// CCC: MEM field:
// Table1 (when MOD = 00)
//    000: [BX+SI]      001: [BX+DI]     010: [BP+SI]           011: [BP+DI]
//    100: [SI]         101: [DI]        110: direct address    111: [BX] 
// Table2 (when MOD = 01 or 10):
//    000: [BX+SI+Offset]     001: [BX+DI+Offset]     010: [BP+SI+Offset]     011: [BP+DI+Offset]
//    100: [SI+Offset]        101: [DI+Offset]        110: [BP+Offset]        111: [BX+Offset]
// CCC can also be another reg if MOD = 11, in which case its contents are interpreded same as BBB
enum ModRM : Byte {
    // MOD
    MODRM_MOD_NODISP = 0b00000000, 
    MODRM_MOD_DISP8  = 0b01000000, 
    MODRM_MOD_DISP16 = 0b10000000, 
    MODRM_MOD_REG    = 0b11000000, 
    MODRM_MOD_MASK   = 0b11000000,
    // REG
    MODRM_REG_AL     = 0b000000, MODRM_REG_AX = MODRM_REG_AL, MODRM_REG_ES = MODRM_REG_AL, MODRM_REG_NONE = MODRM_REG_AL,
    MODRM_REG_CL     = 0b001000, MODRM_REG_CX = MODRM_REG_CL, MODRM_REG_CS = MODRM_REG_CL,
    MODRM_REG_DL     = 0b010000, MODRM_REG_DX = MODRM_REG_DL, MODRM_REG_SS = MODRM_REG_DL,
    MODRM_REG_BL     = 0b011000, MODRM_REG_BX = MODRM_REG_BL, MODRM_REG_DS = MODRM_REG_BL,
    MODRM_REG_AH     = 0b100000, MODRM_REG_SP = MODRM_REG_AH,
    MODRM_REG_CH     = 0b101000, MODRM_REG_BP = MODRM_REG_CH,
    MODRM_REG_DH     = 0b110000, MODRM_REG_SI = MODRM_REG_DH,
    MODRM_REG_BH     = 0b111000, MODRM_REG_DI = MODRM_REG_BH, 
    MODRM_REG_MASK   = 0b111000,
    // MEM
    MODRM_MEM_BX_SI  = 0b000, MODRM_MEM_BX_SI_OFF = MODRM_MEM_BX_SI,
    MODRM_MEM_BX_DI  = 0b001, MODRM_MEM_BX_DI_OFF = MODRM_MEM_BX_DI,
    MODRM_MEM_BP_SI  = 0b010, MODRM_MEM_BP_SI_OFF = MODRM_MEM_BP_SI,
    MODRM_MEM_BP_DI  = 0b011, MODRM_MEM_BP_DI_OFF = MODRM_MEM_BP_DI,
    MODRM_MEM_SI     = 0b100, MODRM_MEM_SI_OFF    = MODRM_MEM_SI,
    MODRM_MEM_DI     = 0b101, MODRM_MEM_DI_OFF    = MODRM_MEM_DI,
    MODRM_MEM_ADDR   = 0b110, MODRM_MEM_BP_OFF    = MODRM_MEM_ADDR,
    MODRM_MEM_BX     = 0b111, MODRM_MEM_BX_OFF    = MODRM_MEM_BX, 
    MODRM_MEM_MASK   = 0b111
};

static const Byte MODRM_REG_SHIFT = 3;

// extract mod/reg/mem field from a ModR/M byte value
inline Byte modrm_mod(const Byte modrm) { return modrm & MODRM_MOD_MASK; }
inline Byte modrm_reg(const Byte modrm) { return modrm & MODRM_REG_MASK; }
inline Byte modrm_mem(const Byte modrm) { return modrm & MODRM_MEM_MASK; }

#endif // MODRM_H