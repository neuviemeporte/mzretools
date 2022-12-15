#ifndef MODRM_H
#define MODRM_H

#include "dos/types.h"

// ModR/M byte semantics: it follows some instruction opcodes and has the following bits: AABBBCCC
// AA: MOD field:
//    00: interpret CCC as Table1 to calculate address of memory operand
//    01: interpret CCC as Table2 with 8bit signed displacement to calculate address of memory operand
//    10: interpret CCC as Table2 with 16bit signed displacement to calculate address of memory operand
//    11: interpret CCC as REG to use register as operand
// BBB: REG field:
//    8bit instruction:  000: AL     001: CL     010: DL     011: BL     100: AH     101: CH     110: DH     111: BH
//    16bit instruction: 000: AX     001: CX     010: DX     011: BX     100: SP     101: BP     110: SI     111: DI
//    segment register:  000: ES     001: CS     010: SS     011: DS
// BBB has special meaning of GRP in ModR/M bytes following so-called group opcodes, depending on the group number the BBB bits encode an instruction type
//    GRP1:  000: ADD    001: OR     010: ADC    011: SBB    100: AND    101: SUB    110: XOR    111: CMP
//    GRP2:  000: ROL    001: ROR    010: RCL    011: RCR    100: SHL    101: SHR    110: ---    111: SAR
//    GRP3a: 000: TEST*  001: ---    010: NOT    011: NEG    100: MUL    101: IMUL   110: DIV    111: IDIV
//    GRP3b: 000: TEST*  001: ---    010: NOT    011: NEG    100: MUL    101: IMUL   110: DIV    111: IDIV
//    GRP4:  000: INC    001: DEC    010: ---    011: ---    100: ---    101: ---    110: ---    111: ---
//    GRP5:  000: INC    001: DEC    010: CALL   011: CALL*  100: JMP    101: JMP*   110: PUSH   111: ---
// *) GRP3a TEST has 8bit operands, GRP3b TEST has 16bit, GRP5 CALL and JMP far variants
// CCC: MEM field:
// Table1 (when MOD = 00)
//    000: [BX+SI]      001: [BX+DI]     010: [BP+SI]           011: [BP+DI]
//    100: [SI]         101: [DI]        110: direct address    111: [BX] 
// Table2 (when MOD = 01 or 10):
//    000: [BX+SI+Offset]     001: [BX+DI+Offset]     010: [BP+SI+Offset]     011: [BP+DI+Offset]
//    100: [SI+Offset]        101: [DI+Offset]        110: [BP+Offset]        111: [BX+Offset]
// CCC can also be another reg if MOD = 11, in which case its contents are interpreded same as BBB for the REG field case
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
    // GRP
    MODRM_GRP1_ADD   = 0b000000, MODRM_GRP2_ROL = MODRM_GRP1_ADD, MODRM_GRP3_TEST = MODRM_GRP1_ADD,  MODRM_GRP4_INC = MODRM_GRP1_ADD, MODRM_GRP5_INC     = MODRM_GRP1_ADD,
    MODRM_GRP1_OR    = 0b001000, MODRM_GRP2_ROR = MODRM_GRP1_OR,                                     MODRM_GRP4_DEC = MODRM_GRP1_OR,  MODRM_GRP5_DEC     = MODRM_GRP1_OR ,
    MODRM_GRP1_ADC   = 0b010000, MODRM_GRP2_RCL = MODRM_GRP1_ADC, MODRM_GRP3_NOT  = MODRM_GRP1_ADC,                                   MODRM_GRP5_CALL    = MODRM_GRP1_ADC,
    MODRM_GRP1_SBB   = 0b011000, MODRM_GRP2_RCR = MODRM_GRP1_SBB, MODRM_GRP3_NEG  = MODRM_GRP1_SBB,                                   MODRM_GRP5_CALL_Mp = MODRM_GRP1_SBB,
    MODRM_GRP1_AND   = 0b100000, MODRM_GRP2_SHL = MODRM_GRP1_AND, MODRM_GRP3_MUL  = MODRM_GRP1_AND,                                   MODRM_GRP5_JMP     = MODRM_GRP1_AND,
    MODRM_GRP1_SUB   = 0b101000, MODRM_GRP2_SHR = MODRM_GRP1_SUB, MODRM_GRP3_IMUL = MODRM_GRP1_SUB,                                   MODRM_GRP5_JMP_Mp  = MODRM_GRP1_SUB,
    MODRM_GRP1_XOR   = 0b110000,                                  MODRM_GRP3_DIV  = MODRM_GRP1_XOR,                                   MODRM_GRP5_PUSH    = MODRM_GRP1_XOR,
    MODRM_GRP1_CMP   = 0b111000, MODRM_GRP2_SAR = MODRM_GRP1_CMP, MODRM_GRP3_IDIV = MODRM_GRP1_CMP, 
    MODRM_GRP_MASK   = MODRM_REG_MASK,
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

static constexpr Byte MODRM_REG_SHIFT = 3;

// extract mod/reg/mem field from a ModR/M byte value
inline Byte modrm_mod(const Byte modrm) { return modrm & MODRM_MOD_MASK; }
inline Byte modrm_reg(const Byte modrm) { return modrm & MODRM_REG_MASK; }
inline Byte modrm_grp(const Byte modrm) { return modrm & MODRM_GRP_MASK; }
inline Byte modrm_mem(const Byte modrm) { return modrm & MODRM_MEM_MASK; }

#endif // MODRM_H