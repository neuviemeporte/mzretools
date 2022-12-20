#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "dos/types.h"
#include "dos/registers.h"
#include "dos/address.h"

#include <string>

enum InstructionClass {
    INS_ERR, // invalid instruction class
    INS_ADD,
    INS_PUSH,
    INS_POP,
    INS_OR,
    INS_ADC,
    INS_SBB,
    INS_AND,
    INS_DAA,
    INS_SUB,
    INS_DAS,
    INS_XOR,
    INS_AAA,
    INS_CMP,
    INS_AAS,
    INS_INC,
    INS_DEC,
    INS_JMP, // all jumps, including conditional
    INS_TEST,
    INS_XCHG,
    INS_MOV,
    INS_LEA,
    INS_NOP,
    INS_CBW, 
    INS_CWD,
    INS_CALL,
    INS_WAIT,
    INS_PUSHF, 
    INS_POPF,
    INS_SAHF, 
    INS_LAHF,
    INS_MOVSB, 
    INS_MOVSW,
    INS_CMPSB, 
    INS_CMPSW,
    INS_STOSB, 
    INS_STOSW,
    INS_LODSB, 
    INS_LODSW,
    INS_SCASB, 
    INS_SCASW,
    INS_RET,
    INS_LES, 
    INS_LDS,
    INS_RETF,
    INS_INT,
    INS_INTO,
    INS_IRET,
    INS_AAM,
    INS_AAD,
    INS_XLAT,
    INS_LOOPNZ, 
    INS_LOOPZ, 
    INS_LOOP, 
    INS_JCXZ, 
    INS_IN, 
    INS_OUT,
    INS_LOCK,
    INS_REPNZ, 
    INS_REPZ,
    INS_HLT,
    INS_CMC,
    INS_CLC, 
    INS_STC,
    INS_CLI, 
    INS_STI,
    INS_CLD, 
    INS_STD,
    // from group opcodes only
    INS_ROL,
    INS_ROR,
    INS_RCL,
    INS_RCR,
    INS_SHL,
    INS_SHR,
    INS_SAR,
    INS_NOT,
    INS_NEG,
    INS_MUL,
    INS_IMUL,
    INS_DIV,
    INS_IDIV,
};

enum InstructionPrefix {
    PRF_NONE,
    PRF_SEG_ES,
    PRF_SEG_CS,
    PRF_SEG_SS,
    PRF_SEG_DS,
    PRF_CHAIN_REPNZ,
    PRF_CHAIN_REPZ,
}

enum OperandType : Byte {
    OPR_ERR,  // operand error
    OPR_NONE, // instruction has no operand
    OPR_REG_AX,
    OPR_REG_AL,
    OPR_REG_AH,
    OPR_REG_BX,
    OPR_REG_BL,
    OPR_REG_BH,
    OPR_REG_CX,
    OPR_REG_CL,
    OPR_REG_CH,
    OPR_REG_DX,
    OPR_REG_DL,
    OPR_REG_DH,
    OPR_REG_SI,
    OPR_REG_DI,
    OPR_REG_BP,
    OPR_REG_SP,
    OPR_REG_CS,
    OPR_REG_DS,
    OPR_REG_ES,
    OPR_REG_SS,
    OPR_MEM_BX_SI,
    OPR_MEM_BX_DI,
    OPR_MEM_BP_SI,
    OPR_MEM_BP_DI,
    OPR_MEM_SI,
    OPR_MEM_DI,
    OPR_MEM_BX,
    OPR_MEM_OFF8,
    OPR_MEM_BX_SI_OFF8,
    OPR_MEM_BX_DI_OFF8,
    OPR_MEM_BP_SI_OFF8,
    OPR_MEM_BP_DI_OFF8,
    OPR_MEM_SI_OFF8,
    OPR_MEM_DI_OFF8,
    OPR_MEM_BP_OFF8,
    OPR_MEM_BX_OFF8,
    OPR_MEM_OFF16,
    OPR_MEM_BX_SI_OFF16,
    OPR_MEM_BX_DI_OFF16,
    OPR_MEM_BP_SI_OFF16,
    OPR_MEM_BP_DI_OFF16,
    OPR_MEM_SI_OFF16,
    OPR_MEM_DI_OFF16,
    OPR_MEM_BP_OFF16,
    OPR_MEM_BX_OFF16,
    OPR_IMM0,
    OPR_IMM1,
    OPR_IMM8,
    OPR_IMM16,
    OPR_IMM32,
};

class Instruction {
public:
    const Byte *data;
    Address addr;
    InstructionPrefix prefix;
    Byte opcode;
    InstructionClass iclass;
    Size length;
    struct Operand {
        OperandType type;
        SWord offset;
        UDWord immval;
    } op1, op2;

    Instruction(const Byte *idata);
    std::string toString() const;

private:
    OperandType getModrmOperand(const Byte modrm, const ModrmOperand op);
    void loadOperand(Operand &op);
};

InstructionClass instr_class(const Byte opcode);

#endif // INSTRUCTION_H