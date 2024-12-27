#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "dos/types.h"
#include "dos/registers.h"
#include "dos/modrm.h"
#include "dos/address.h"
#include "dos/opcodes.h"

#include <string>
#include <vector>

// TODO: rep and repnz are not used as instructions, but prefixes, remove
#define INSTRUCTION_CLASS \
    X(INS_ERR) \
    X(INS_ADD) \
    X(INS_PUSH) \
    X(INS_POP) \
    X(INS_OR) \
    X(INS_ADC) \
    X(INS_SBB) \
    X(INS_AND) \
    X(INS_DAA) \
    X(INS_SUB) \
    X(INS_DAS) \
    X(INS_XOR) \
    X(INS_AAA) \
    X(INS_CMP) \
    X(INS_AAS) \
    X(INS_INC) \
    X(INS_DEC) \
    X(INS_JMP) \
    X(INS_JMP_IF) \
    X(INS_JMP_FAR) \
    X(INS_TEST) \
    X(INS_XCHG) \
    X(INS_MOV) \
    X(INS_LEA) \
    X(INS_NOP) \
    X(INS_CBW) \
    X(INS_CWD) \
    X(INS_CALL) \
    X(INS_CALL_FAR) \
    X(INS_WAIT) \
    X(INS_PUSHF) \
    X(INS_POPF) \
    X(INS_SAHF) \
    X(INS_LAHF) \
    X(INS_MOVSB) \
    X(INS_MOVSW) \
    X(INS_CMPSB) \
    X(INS_CMPSW) \
    X(INS_STOSB) \
    X(INS_STOSW) \
    X(INS_LODSB) \
    X(INS_LODSW) \
    X(INS_SCASB) \
    X(INS_SCASW) \
    X(INS_RET) \
    X(INS_LES) \
    X(INS_LDS) \
    X(INS_RETF) \
    X(INS_INT) \
    X(INS_INT3) \
    X(INS_INTO) \
    X(INS_IRET) \
    X(INS_AAM) \
    X(INS_AAD) \
    X(INS_XLAT) \
    X(INS_LOOPNZ) \
    X(INS_LOOPZ) \
    X(INS_LOOP) \
    X(INS_IN) \
    X(INS_OUT) \
    X(INS_LOCK) \
    X(INS_REPNZ) \
    X(INS_REPZ) \
    X(INS_HLT) \
    X(INS_CMC) \
    X(INS_CLC) \
    X(INS_STC) \
    X(INS_CLI) \
    X(INS_STI) \
    X(INS_CLD) \
    X(INS_STD) \
    X(INS_ROL) \
    X(INS_ROR) \
    X(INS_RCL) \
    X(INS_RCR) \
    X(INS_SHL) \
    X(INS_SHR) \
    X(INS_SAR) \
    X(INS_NOT) \
    X(INS_NEG) \
    X(INS_MUL) \
    X(INS_IMUL) \
    X(INS_DIV) \
    X(INS_IDIV)
enum InstructionClass {
#define X(x) x,
INSTRUCTION_CLASS
#undef X
};

#define INSTRUCTION_PREFIX \
    X(PRF_NONE) \
    X(PRF_SEG_ES) \
    X(PRF_SEG_CS) \
    X(PRF_SEG_SS) \
    X(PRF_SEG_DS) \
    X(PRF_CHAIN_REPNZ) \
    X(PRF_CHAIN_REPZ)
enum InstructionPrefix {
#define X(x) x,
INSTRUCTION_PREFIX
#undef X
};

inline bool prefixIsSegment(const InstructionPrefix p) { return p >= PRF_SEG_ES && p <= PRF_SEG_DS; }
Register prefixRegId(const InstructionPrefix p);

// the offsets in memory operands (OFF8/OFF16) are signed, except for OPR_MEM_OFF16 (e.g. [0x9000])
// TODO: OPR_MEM_OFF8 should not exist? There is no modrm MEM type that allows a 8bit offset by itself.
#define OPERAND_TYPE \
    X(OPR_ERR) \
    X(OPR_NONE) \
    X(OPR_REG_AX) \
    X(OPR_REG_AL) \
    X(OPR_REG_AH) \
    X(OPR_REG_BX) \
    X(OPR_REG_BL) \
    X(OPR_REG_BH) \
    X(OPR_REG_CX) \
    X(OPR_REG_CL) \
    X(OPR_REG_CH) \
    X(OPR_REG_DX) \
    X(OPR_REG_DL) \
    X(OPR_REG_DH) \
    X(OPR_REG_SI) \
    X(OPR_REG_DI) \
    X(OPR_REG_BP) \
    X(OPR_REG_SP) \
    X(OPR_REG_CS) \
    X(OPR_REG_DS) \
    X(OPR_REG_ES) \
    X(OPR_REG_SS) \
    X(OPR_MEM_BX_SI) \
    X(OPR_MEM_BX_DI) \
    X(OPR_MEM_BP_SI) \
    X(OPR_MEM_BP_DI) \
    X(OPR_MEM_SI) \
    X(OPR_MEM_DI) \
    X(OPR_MEM_BX) \
    X(OPR_MEM_OFF8) \
    X(OPR_MEM_BX_SI_OFF8) \
    X(OPR_MEM_BX_DI_OFF8) \
    X(OPR_MEM_BP_SI_OFF8) \
    X(OPR_MEM_BP_DI_OFF8) \
    X(OPR_MEM_SI_OFF8) \
    X(OPR_MEM_DI_OFF8) \
    X(OPR_MEM_BP_OFF8) \
    X(OPR_MEM_BX_OFF8) \
    X(OPR_MEM_OFF16) \
    X(OPR_MEM_BX_SI_OFF16) \
    X(OPR_MEM_BX_DI_OFF16) \
    X(OPR_MEM_BP_SI_OFF16) \
    X(OPR_MEM_BP_DI_OFF16) \
    X(OPR_MEM_SI_OFF16) \
    X(OPR_MEM_DI_OFF16) \
    X(OPR_MEM_BP_OFF16) \
    X(OPR_MEM_BX_OFF16) \
    X(OPR_IMM0) \
    X(OPR_IMM1) \
    X(OPR_IMM8) \
    X(OPR_IMM16) \
    X(OPR_IMM32)
enum OperandType : Byte {
#define X(x) x,
OPERAND_TYPE
#undef X
};

inline bool operandIsReg(const OperandType type) {
    return type >= OPR_REG_AX && type <= OPR_REG_SS;
}

inline bool operandIsMem(const OperandType type) {
    return type >= OPR_MEM_BX_SI && type <= OPR_MEM_BX_OFF16;
}

inline bool operandIsMemWithOffset(const OperandType type) {
    return type >= OPR_MEM_OFF8 && type <= OPR_MEM_BX_OFF16;
}

inline bool operandIsMemNoOffset(const OperandType type) {
    return type >= OPR_MEM_BX_SI && type <= OPR_MEM_BX;
}

inline bool operandIsMemImmediate(const OperandType ot) {
    return ot == OPR_MEM_OFF8 || ot == OPR_MEM_OFF16;
}

inline bool operandIsMemWithByteOffset(const OperandType type) {
    return type >= OPR_MEM_OFF8 && type <= OPR_MEM_BX_OFF8;
}

inline bool operandIsMemWithWordOffset(const OperandType type) {
    return type >= OPR_MEM_OFF16 && type <= OPR_MEM_BX_OFF16;
}

inline bool operandIsImmediate(const OperandType ot) {
    return ot >= OPR_IMM0 && ot <= OPR_IMM32;
}

// not an implicit immediate, i.e. not OPR_IMM0 or OPR_IMM1
inline bool operandIsExplicitImmediate(const OperandType ot) {
    return ot >= OPR_IMM8 && ot <= OPR_IMM32;
}

OperandType operandTypeToWord(const OperandType ot);

Register defaultMemSegment(const OperandType ot);

#define OPERAND_SIZE \
    X(OPRSZ_UNK) \
    X(OPRSZ_NONE) \
    X(OPRSZ_BYTE) \
    X(OPRSZ_WORD) \
    X(OPRSZ_DWORD)
enum OperandSize {
#define X(x) x,
OPERAND_SIZE
#undef X
};

#define INS_GROUP_IDX \
    X(IGRP_1) \
    X(IGRP_2) \
    X(IGRP_3a) \
    X(IGRP_3b) \
    X(IGRP_4) \
    X(IGRP_5) \
    X(IGRP_BAD)
enum InstructionGroupIndex {
#define X(x) x,
INS_GROUP_IDX
#undef X
};

#define INSTRUCTION_MATCH \
    X(INS_MATCH_ERROR) \
    X(INS_MATCH_FULL) \
    X(INS_MATCH_DIFF) \
    X(INS_MATCH_DIFFOP1) \
    X(INS_MATCH_DIFFOP2) \
    X(INS_MATCH_MISMATCH)
enum InstructionMatch {
#define X(x) x,
INSTRUCTION_MATCH
#undef X
};

class Instruction {
public:
    Address addr;
    InstructionPrefix prefix;
    Byte opcode;
    InstructionClass iclass;
    Byte length;
    struct Operand {
        OperandType type;
        // the size of the operand is unrelated to the length of the optional immediate value
        // TODO: the enum OperandSize is used not only for the operand's size here, find a better name
        OperandSize size, immsize;
        union {
            DWord u32;
            Word u16;
            Byte u8;
        } immval; // optional immediate offset or literal value

        std::string toString() const;
        InstructionMatch match(const Operand &other) const;
        Register regId() const;
        Word wordValue() const;
        DWord dwordValue() const;
        Address farAddr() const;
        ByteString immediateValue() const;
    } op1, op2;
    const Byte* data;

    Instruction();
    Instruction(const Address &addr, const Byte *data);
    std::string toString(const bool extended = false) const;
    ByteString pattern() const;
    InstructionMatch match(const Instruction &other) const;
    void load(const Byte *data);
    Word absoluteOffset() const;
    Address destinationAddress() const;
    SWord relativeOffset() const;
    std::vector<Register> touchedRegs() const;
    SOffset memOffset() const;
    Register memSegmentId() const;

    bool isValid() const { return iclass != INS_ERR; }
    bool isJump() const { return iclass == INS_JMP || iclass == INS_JMP_IF || iclass == INS_JMP_FAR; }
    bool isUnconditionalJump() const { return iclass == INS_JMP || iclass == INS_JMP_FAR; }
    bool isCall() const { return iclass == INS_CALL || iclass == INS_CALL_FAR; }
    bool isFarCall() const { return iclass == INS_CALL_FAR; }
    bool isLoop() const { return iclass == INS_LOOP || iclass == INS_LOOPZ || iclass == INS_LOOPNZ; }
    bool isBranch() const { return isJump() || isCall() || isLoop(); }
    bool isNearJump() const { return iclass == INS_JMP || iclass == INS_JMP_IF || isLoop(); }
    bool isNearBranch() const { return isNearJump() || iclass == INS_CALL; }
    bool isReturn() const { return iclass == INS_RET || iclass == INS_RETF || iclass == INS_IRET; }
    bool isInt(const Byte n) { 
        return (iclass == INS_INT && op1.immval.u8 == n) || (iclass == INS_INT3 && n == 3) || (iclass == INS_INTO && n == 4); 
    }

private:
    OperandType getModrmOperand(const Byte modrm, const ModrmOperand op);
    Size loadImmediate(Operand &op, const Byte *data);
    const Operand* memOperand() const;
};

InstructionClass instr_class(const Byte opcode);
const char* instr_class_name(const InstructionClass iclass);

#endif // INSTRUCTION_H