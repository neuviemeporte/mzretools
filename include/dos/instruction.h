#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "dos/types.h"
#include "dos/registers.h"
#include "dos/address.h"

#include <string>

enum InstructionClass {
    INS_NONE,
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
    INS_JMP, // all unconditional jumps
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
    INS_STD
};

enum OperandType {
    OPR_NONE, OPR_REG, OPR_MEM, OPR_IMM
};

enum MemoryType {
    MEM_BX_SI,
    MEM_BX_DI,
    MEM_BP_SI,
    MEM_BP_DI,
    MEM_SI,
    MEM_DI,
    MEM_DIRECT,
    MEM_BX,
//    000: [BX+SI]      001: [BX+DI]     010: [BP+SI]           011: [BP+DI]
//    100: [SI]         101: [DI]        110: direct address    111: [BX] 
// Table2 (when MOD = 01 or 10):
//    000: [BX+SI+Offset]     001: [BX+DI+Offset]     010: [BP+SI+Offset]     011: [BP+DI+Offset]
//    100: [SI+Offset]        101: [DI+Offset]        110: [BP+Offset]        111: [BX+Offset]
};

struct Instruction {
    Address addr;
    InstructionClass clss;
    OperandType srcType, dstType;
    union Operand {
        Register regId;
        MemoryType memType;
        Byte imm8;
        Word imm16;
    } op1, op2;
    std::string toString() const;
};

InstructionClass instr_class(const Byte opcode);

#endif // INSTRUCTION_H