#include "dos/instruction.h"
#include "dos/opcodes.h"
#include "dos/error.h"
#include "dos/util.h"
#include "dos/output.h"

#include <sstream>

using namespace std;

// TODO: handle invalid opcodes gracefully, don't throw/assert

#define DEBUG(msg) output(msg, LOG_CPU, LOG_DEBUG)

#define X(x) #x,
static const char* INS_CLASS_ID[] = {
INSTRUCTION_CLASS
};
static const char* INS_PRF_ID[] = {
INSTRUCTION_PREFIX
};
static const char* OPR_TYPE_ID[] = {
OPERAND_TYPE
};
static const char* OPR_SIZE_ID[] = {
OPERAND_SIZE
};
static const char* GRP_IDX_ID[] = {
INS_GROUP_IDX
};
static const char* INS_MATCH_ID[] = {
INSTRUCTION_MATCH
};
static const char* MODRM_OPR_ID[] = {
MODRM_OPERAND
};
#undef X


// maps (non-group) opcodes to an instruction class
static const InstructionClass OPCODE_CLASS[] = {
//   0         1          2          3         4          5          6         7           8         9         A             B          C          D          E          F
INS_ADD,    INS_ADD,   INS_ADD,   INS_ADD,  INS_ADD,   INS_ADD,   INS_PUSH,  INS_POP,   INS_OR,   INS_OR,   INS_OR,       INS_OR,    INS_OR,    INS_OR,    INS_PUSH,  INS_ERR,   // 0
INS_ADC,    INS_ADC,   INS_ADC,   INS_ADC,  INS_ADC,   INS_ADC,   INS_PUSH,  INS_POP,   INS_SBB,  INS_SBB,  INS_SBB,      INS_SBB,   INS_SBB,   INS_SBB,   INS_PUSH,  INS_POP,   // 1
INS_AND,    INS_AND,   INS_AND,   INS_AND,  INS_AND,   INS_AND,   INS_ERR,   INS_DAA,   INS_SUB,  INS_SUB,  INS_SUB,      INS_SUB,   INS_SUB,   INS_SUB,   INS_ERR,   INS_DAS,   // 2
INS_XOR,    INS_XOR,   INS_XOR,   INS_XOR,  INS_XOR,   INS_XOR,   INS_ERR,   INS_AAA,   INS_CMP,  INS_CMP,  INS_CMP,      INS_CMP,   INS_CMP,   INS_CMP,   INS_ERR,   INS_AAS,   // 3
INS_INC,    INS_INC,   INS_INC,   INS_INC,  INS_INC,   INS_INC,   INS_INC,   INS_INC,   INS_DEC,  INS_DEC,  INS_DEC,      INS_DEC,   INS_DEC,   INS_DEC,   INS_DEC,   INS_DEC,   // 4
INS_PUSH,   INS_PUSH,  INS_PUSH,  INS_PUSH, INS_PUSH,  INS_PUSH,  INS_PUSH,  INS_PUSH,  INS_POP,  INS_POP,  INS_POP,      INS_POP,   INS_POP,   INS_POP,   INS_POP,   INS_POP,   // 5
INS_ERR,    INS_ERR,   INS_ERR,   INS_ERR,  INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,  INS_ERR,  INS_ERR,      INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   // 6
INS_JMP,    INS_JMP,   INS_JMP,   INS_JMP,  INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,  INS_JMP,  INS_JMP,      INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,   // 7
INS_ERR,    INS_ERR,   INS_ERR,   INS_ERR,  INS_TEST,  INS_TEST,  INS_XCHG,  INS_XCHG,  INS_MOV,  INS_MOV,  INS_MOV,      INS_MOV,   INS_MOV,   INS_LEA,   INS_MOV,   INS_POP,   // 8
INS_NOP,    INS_XCHG,  INS_XCHG,  INS_XCHG, INS_XCHG,  INS_XCHG,  INS_XCHG,  INS_XCHG,  INS_CBW,  INS_CWD,  INS_CALL_FAR, INS_WAIT,  INS_PUSHF, INS_POPF,  INS_SAHF,  INS_LAHF,  // 9
INS_MOV,    INS_MOV,   INS_MOV,   INS_MOV,  INS_MOVSB, INS_MOVSW, INS_CMPSB, INS_CMPSW, INS_TEST, INS_TEST, INS_STOSB,    INS_STOSW, INS_LODSB, INS_LODSW, INS_SCASB, INS_SCASW, // A
INS_MOV,    INS_MOV,   INS_MOV,   INS_MOV,  INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,  INS_MOV,  INS_MOV,      INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,   // B
INS_ERR,    INS_ERR,   INS_RET,   INS_RET,  INS_LES,   INS_LDS,   INS_MOV,   INS_MOV,   INS_ERR,  INS_ERR,  INS_RETF,     INS_RETF,  INS_INT,   INS_INT,   INS_INTO,  INS_IRET,  // C
INS_ERR,    INS_ERR,   INS_ERR,   INS_ERR,  INS_AAM,   INS_AAD,   INS_ERR,   INS_XLAT,  INS_ERR,  INS_ERR,  INS_ERR,      INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   // D
INS_LOOPNZ, INS_LOOPZ, INS_LOOP,  INS_JCXZ, INS_IN,    INS_IN,    INS_OUT,   INS_OUT,   INS_CALL, INS_JMP,  INS_JMP,      INS_JMP,   INS_IN,    INS_IN,    INS_OUT,   INS_OUT,   // E
INS_LOCK,   INS_ERR,   INS_REPNZ, INS_REPZ, INS_HLT,   INS_CMC,   INS_ERR,   INS_ERR,   INS_CLC,  INS_STC,  INS_CLI,      INS_STI,   INS_CLD,   INS_STD,   INS_ERR,   INS_ERR,   // F
};

// maps group opcodes to group indexes in the next table
static const InstructionGroupIndex GRP_IDX[0x100] = {
//  0         1         2         3         4         5         6         7         8         9         A         B         C         D         E         F
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 0
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 1
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 2
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 3
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 4
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 5
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 6
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 7
IGRP_1,   IGRP_1,   IGRP_1,   IGRP_1,   IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 8
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // 9
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // A
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // B
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // C
IGRP_2,   IGRP_2,   IGRP_2,   IGRP_2,   IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // D
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, // E
IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_3a,  IGRP_3b,  IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_BAD, IGRP_4,   IGRP_5,   // F
};

// maps a group index and the GRP value from a modrm byte to an instruction class for a group opcode
static const InstructionClass GRP_INS_CLASS[6][8] = {
    INS_ADD,  INS_OR,  INS_ADC,  INS_SBB,      INS_AND, INS_SUB,      INS_XOR,  INS_CMP,  // GRP1
    INS_ROL,  INS_ROR, INS_RCL,  INS_RCR,      INS_SHL, INS_SHR,      INS_ERR,  INS_SAR,  // GRP2
    INS_TEST, INS_ERR, INS_NOT,  INS_NEG,      INS_MUL, INS_IMUL,     INS_DIV,  INS_IDIV, // GRP3a
    INS_TEST, INS_ERR, INS_NOT,  INS_NEG,      INS_MUL, INS_IMUL,     INS_DIV,  INS_IDIV, // GRP3b
    INS_INC,  INS_DEC, INS_ERR,  INS_ERR,      INS_ERR, INS_ERR,      INS_ERR,  INS_ERR,  // GRP4
    INS_INC,  INS_DEC, INS_CALL, INS_CALL_FAR, INS_JMP, INS_JMP_FAR,  INS_PUSH, INS_ERR,  // GRP5
};

// maps non-modrm opcodes into their first operand's type
static const OperandType OP1_TYPE[] = {
//   0         1           2             3             4           5           6           7           8           9           A           B           C           D           E           F
OPR_ERR,    OPR_ERR,    OPR_ERR,      OPR_ERR,      OPR_REG_AL, OPR_REG_AX, OPR_REG_ES, OPR_REG_ES, OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_REG_CS, OPR_ERR,    // 0
OPR_ERR,    OPR_ERR,    OPR_ERR,      OPR_ERR,      OPR_REG_AL, OPR_REG_AX, OPR_REG_SS, OPR_REG_SS, OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_REG_DS, OPR_REG_DS, // 1
OPR_ERR,    OPR_ERR,    OPR_ERR,      OPR_ERR,      OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   // 2
OPR_ERR,    OPR_ERR,    OPR_ERR,      OPR_ERR,      OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   // 3
OPR_REG_AX, OPR_REG_CX, OPR_REG_DX,   OPR_REG_BX,   OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // 4
OPR_REG_AX, OPR_REG_CX, OPR_REG_DX,   OPR_REG_BX,   OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // 5
OPR_ERR,    OPR_ERR,    OPR_ERR,      OPR_ERR,      OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 6
OPR_IMM8,   OPR_IMM8,   OPR_IMM8,     OPR_IMM8,     OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   // 7
OPR_ERR,    OPR_ERR,    OPR_ERR,      OPR_ERR,      OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 8
OPR_NONE,   OPR_REG_CX, OPR_REG_DX,   OPR_REG_BX,   OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_NONE,   OPR_NONE,   OPR_IMM32,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 9
OPR_REG_AL, OPR_REG_AX, OPR_MEM_OFF8, OPR_MEM_OFF16, OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_REG_AL, OPR_REG_AX, OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // A
OPR_REG_AL, OPR_REG_CL, OPR_REG_DL,   OPR_REG_BL,   OPR_REG_AH, OPR_REG_CH, OPR_REG_DH, OPR_REG_BH, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // B
OPR_ERR,    OPR_ERR,    OPR_IMM16,    OPR_NONE,     OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_IMM8,   OPR_NONE,   OPR_NONE,   // C
OPR_ERR,    OPR_ERR,    OPR_ERR,      OPR_ERR,      OPR_IMM0,   OPR_IMM0,   OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // D
OPR_IMM8,   OPR_IMM8,   OPR_IMM8,     OPR_IMM8,     OPR_REG_AL, OPR_REG_AX, OPR_IMM8,   OPR_IMM8,   OPR_IMM16,  OPR_IMM16,  OPR_IMM32,  OPR_IMM8,   OPR_REG_AL, OPR_REG_AX, OPR_REG_DX, OPR_REG_DX, // E
OPR_NONE,   OPR_NONE,   OPR_NONE,     OPR_NONE,     OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    // F
};

// maps non-modrm opcodes into their first operand's type
static const OperandType OP2_TYPE[] = {
//   0           1              2           3           4           5           6           7           8          9          A          B          C           D           E           F
OPR_ERR,      OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_ERR,    // 0
OPR_ERR,      OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   // 1
OPR_ERR,      OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_ERR,    OPR_NONE,   // 2
OPR_ERR,      OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_ERR,    OPR_NONE,   // 3
OPR_NONE,     OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 4
OPR_NONE,     OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 5
OPR_ERR,      OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 6
OPR_NONE,     OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 7
OPR_ERR,      OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 8
OPR_NONE,     OPR_REG_AX,    OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 9
OPR_MEM_OFF8, OPR_MEM_OFF16, OPR_REG_AL, OPR_REG_AX, OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_IMM8,  OPR_IMM16, OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // A
OPR_IMM8,     OPR_IMM8,      OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM16, OPR_IMM16, OPR_IMM16, OPR_IMM16, OPR_IMM16,  OPR_IMM16,  OPR_IMM16,  OPR_IMM16,  // B
OPR_ERR,      OPR_ERR,       OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // C
OPR_ERR,      OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // D
OPR_NONE,     OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_IMM8,   OPR_IMM8,   OPR_REG_AL, OPR_REG_AX, OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_REG_DX, OPR_REG_DX, OPR_REG_AL, OPR_REG_AX, // E
OPR_NONE,     OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    // F
};

// map operand type to operand size
static const OperandSize OPR_SIZE[] = {
    OPRSZ_UNK,  OPRSZ_NONE, // error, none
    OPRSZ_WORD, OPRSZ_BYTE, OPRSZ_BYTE, // ax, al, ah
    OPRSZ_WORD, OPRSZ_BYTE, OPRSZ_BYTE, // bx, bl, bh
    OPRSZ_WORD, OPRSZ_BYTE, OPRSZ_BYTE, // cx, cl, ch 
    OPRSZ_WORD, OPRSZ_BYTE, OPRSZ_BYTE, // dx, dl, dh
    OPRSZ_WORD, OPRSZ_WORD, OPRSZ_WORD, OPRSZ_WORD, // si, di, bp, sp 
    OPRSZ_WORD, OPRSZ_WORD, OPRSZ_WORD, OPRSZ_WORD, // cs, ds, es, ss
    OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, 
    OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, 
    OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, 
    OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, OPRSZ_UNK, // all memory operands
    OPRSZ_UNK, // immediate 0
    OPRSZ_UNK, // immediate 1
    OPRSZ_BYTE,
    OPRSZ_WORD,
    OPRSZ_DWORD,
};

// map modrm operand type to operand size
static const OperandSize MODRM_OPR_SIZE[] = {
    OPRSZ_NONE,  // MODRM_NONE
    OPRSZ_BYTE,  // MODRM_Eb
    OPRSZ_BYTE,  // MODRM_Gb
    OPRSZ_BYTE,  // MODRM_Ib
    OPRSZ_WORD,  // MODRM_Ev
    OPRSZ_WORD,  // MODRM_Gv
    OPRSZ_WORD,  // MODRM_Iv
    OPRSZ_WORD,  // MODRM_Sw
    OPRSZ_UNK,   // MODRM_M
    OPRSZ_DWORD, // MODRM_Mp
    OPRSZ_UNK,   // MODRM_1
    OPRSZ_BYTE,  // MODRM_CL
};

InstructionClass instr_class(const Byte opcode) {
    return OPCODE_CLASS[opcode];
}

Instruction::Instruction() : data(nullptr), addr{}, prefix(PRF_NONE), opcode(OP_INVALID), iclass(INS_ERR), length(0) {
}

Instruction::Instruction(const Address &addr, const Byte *idata) : addr{addr}, data(idata), prefix(PRF_NONE), opcode(OP_INVALID), iclass(INS_ERR), length(0) {
    load();
}

void Instruction::load()  {
    opcode = *data++;
    length++;
    DEBUG("Parsed opcode: "s + opcodeName(opcode) + ", length = " + to_string(length));
    // in case of a chain opcode, use it to set an appropriate prefix value and replace the opcode with the subsequent instruction
    // TODO: support LOCK, other prefix-like opcodes?
    if (opcode == OP_REPZ || opcode == OP_REPNZ) { 
        prefix = static_cast<InstructionPrefix>(opcode - OP_REPNZ + PRF_CHAIN_REPNZ); // convert opcode to instruction prefix enum
        // TODO: guard against memory overflow
        opcode = *data++;
        length++;
        DEBUG("Found chain prefix "s + INS_PRF_ID[prefix] + ", opcode now: "s + opcodeName(opcode) + ", length = " + to_string(length));
    }
    // likewise in case of a segment ovverride prefix, set instruction prefix value and get next opcode
    else if (opcodeIsSegmentPrefix(opcode)) {
        prefix = static_cast<InstructionPrefix>(((opcode - OP_PREFIX_ES) / 8) + PRF_SEG_ES); // convert opcode to instruction prefix enum, the segment prefix opcode values differ by 8
        opcode = *data++;
        length++;
        DEBUG("Found segment prefix "s + INS_PRF_ID[prefix] + ", opcode now: "s + opcodeName(opcode) + ", length = " + to_string(length));
    }

    // regular instruction opcode
    if (!opcodeIsModrm(opcode)) {
        iclass = instr_class(opcode);
        DEBUG("regular opcode, class "s + INS_CLASS_ID[iclass]);
        // get type of operands
        op1.type = OP1_TYPE[opcode];
        op2.type = OP2_TYPE[opcode];

        // TODO: do not derive size from operand type, but from opcode, same for modrm and group

        op1.size = OPR_SIZE[op1.type];
        op2.size = OPR_SIZE[op2.type];
    }
    // modr/m insruction opcode
    else if (!opcodeIsGroup(opcode)) {
        const Byte modrm = *data++; // load modrm byte
        length++;
        const ModrmOperand 
            modop1 = modrm_op1(opcode),
            modop2 = modrm_op2(opcode);
        iclass = instr_class(opcode);
        DEBUG("modrm opcode "s + opcodeName(opcode) + ", modrm = " + hexVal(modrm) + ", operand types: op1 = "s + MODRM_OPR_ID[modop1] + ", op2 = " + MODRM_OPR_ID[modop2] + ", class " + INS_CLASS_ID[iclass]);
        // convert from messy modrm operand designation to our nice type
        op1.type = getModrmOperand(modrm, modop1);
        op2.type = getModrmOperand(modrm, modop2);
        op1.size = MODRM_OPR_SIZE[modop1];
        op2.size = MODRM_OPR_SIZE[modop2];
    }
    // group instruction opcode
    else {
        assert(opcodeIsGroup(opcode));
        const Byte modrm = *data++; // load modrm byte
        length++;
        // obtain index of group for instruction class lookup
        const InstructionGroupIndex grpIdx = GRP_IDX[opcode];
        const Byte grpInstrIdx = modrm_grp(modrm) >> MODRM_GRP_SHIFT;
        DEBUG("group opcode "s + opcodeName(opcode) + ", modrm = " + hexVal(modrm) + ", group index " + GRP_IDX_ID[grpIdx] + ", instruction " + hexVal(grpInstrIdx));
        assert(grpIdx >= IGRP_1 && grpIdx <= IGRP_5);
        assert(grpInstrIdx < 8); // groups have up to 8 instructions (index 0-based)
        // determine instruction class
        iclass = GRP_INS_CLASS[grpIdx][grpInstrIdx];
        assert(iclass != INS_ERR);
        // the rest is just like a "normal" modrm opcode
        ModrmOperand 
            modop1 = modrm_op1(opcode),
            modop2 = modrm_op2(opcode);
        // special case for implicit 2nd operand for group 3a/3b TEST instruction
        if (iclass == INS_TEST) {
            switch(grpIdx) {
            case IGRP_3a: modop2 = MODRM_Ib; break;
            case IGRP_3b: modop2 = MODRM_Iv; break;
            default:
                throw CpuError("Invalid group index for TEST opcode");
            }
        }
        // another special case for operand override in group 5 far call and jmp instructions
        else if (iclass == INS_CALL_FAR || iclass == INS_JMP_FAR) {
            assert(grpIdx == IGRP_5);
            modop1 = MODRM_Mp;
        }
        DEBUG("modrm operand types: op1 = "s + MODRM_OPR_ID[modop1] + ", op2 = " + MODRM_OPR_ID[modop2] + ", class " + INS_CLASS_ID[iclass]);            
        // convert from messy modrm operand designation to our nice type
        op1.type = getModrmOperand(modrm, modop1);
        op2.type = getModrmOperand(modrm, modop2);
        op1.size = MODRM_OPR_SIZE[modop1];
        op2.size = MODRM_OPR_SIZE[modop2];
    }

    assert(op1.type != OPR_ERR && op2.type != OPR_ERR);
    DEBUG("generalized operands, op1: type = "s + OPR_TYPE_ID[op1.type] + ", size = " + OPR_SIZE_ID[op1.size] + ", op2: type = " + OPR_TYPE_ID[op2.type] + ", size = " + OPR_SIZE_ID[op2.size]);
    loadOperand(op1);
    loadOperand(op2);

    // don't need it anymore
    data = nullptr; 
    DEBUG("Instruction: "s + toString());
}

static const char* INS_NAME[] = {
    "???", "add", "push", "pop", "or", "adc", "sbb", "and", "daa", "sub", "das", "xor", "aaa", "cmp", "aas", "inc", "dec", "jmp", "jmp far", "test", "xchg", "mov", "lea", "nop", "cbw", "cwd",
    "call", "call far", "wait", "pushf", "popf", "sahf", "lahf", "movsb", "movsw", "cmpsb", "cmpsw", "stosb", "stosw", "lodsb", "lodsw", "scasb", "scasw", "ret", "les", "lds", "retf", "int",
    "into", "iret", "aam", "aad", "xlat", "loopnz", "loopz", "loop", "jcxz", "in", "out", "lock", "repnz", "repz", "hlt", "cmc", "clc", "stc", "cli", "sti", "cld", "std",
    "rol", "ror", "rcl", "rcr", "shl", "shr", "sar", "not", "neg", "mul", "imul", "div", "idiv"
};

static const char* JMP_NAME[16] = {
    "jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "ja", "js", "jns", "jpe", "jpo", "jl", "jge", "jle", "jg",
};

static const char* OPR_NAME[] = {
    "???", "X", 
    "ax", "al", "ah", "bx", "bl", "bh", "cx", "cl", "ch", "dx", "dl", "dh", "si", "di", "bp", "sp", "cs", "ds", "es", "ss",
    "bx+si", "bx+di", "bp+si", "bp+di", "si", "di", "bx", 
    "", "bx+si", "bx+di", "bp+si", "bp+di", "si", "di", "bp", "bx",
    "", "bx+si", "bx+di", "bp+si", "bp+di", "si", "di", "bp", "bx",
    "0", "1", "i8", "i16", "i32",
};

static const char* PRF_NAME[] = {
    "???", "es:", "cs:", "ss:", "ds:", "repnz", "repz"
};

inline bool operandIsReg(const OperandType type) {
    return type >= OPR_REG_AX && type <= OPR_REG_SS;
}

inline bool operandIsMem(const OperandType type) {
    return type >= OPR_MEM_BX_SI && type <= OPR_MEM_BX_OFF16;
}

inline bool operandIsMemNoOffset(const OperandType type) {
    return type >= OPR_MEM_BX_SI && type <= OPR_MEM_BX;
}

inline bool operandIsMemWithByteOffset(const OperandType type) {
    return type >= OPR_MEM_OFF8 && type <= OPR_MEM_BX_OFF8;
}

inline bool operandIsMemWithWordOffset(const OperandType type) {
    return type >= OPR_MEM_OFF16 && type <= OPR_MEM_BX_OFF16;
}

inline bool operandIsImmediate(const OperandType type) {
    return type >= OPR_IMM0 && type <= OPR_IMM32;
}

std::string Instruction::Operand::toString() const {
    ostringstream str;
    if (operandIsReg(type))
        str << OPR_NAME[type];
    else if (operandIsMemNoOffset(type))
        str << "[" << OPR_NAME[type] << "]";
    else if (operandIsMemWithByteOffset(type)) {
        if (type == OPR_MEM_OFF8) str << "[0x" << hex << (int)off.u8 << "]";
        else str << "[" << OPR_NAME[type] << signedHexVal(off.s8) << "]";

    }
    else if (operandIsMemWithWordOffset(type)) {
        if (type == OPR_MEM_OFF16) str << "[0x" << hex << off.u16 << "]";
        else str << "[" << OPR_NAME[type] << signedHexVal(off.s16) << "]";
    }
    else if (type == OPR_IMM0 || type == OPR_IMM1)
        str << OPR_NAME[type];
    else if (type == OPR_IMM8)
        str << hexVal(imm.u8, true, false);
    else if (type == OPR_IMM16)
        str << hexVal(imm.u16, true, false);
    else if (type == OPR_IMM32)
        str << hexVal(imm.u32, true, false);

    return str.str();
}

InstructionMatch Instruction::Operand::match(const Operand &other) {
    if (type != other.type || size != other.size)
        return INS_MATCH_MISMATCH;
    switch(type) {
    case OPR_MEM_OFF8:
    case OPR_MEM_BX_SI_OFF8:
    case OPR_MEM_BX_DI_OFF8:
    case OPR_MEM_BP_SI_OFF8:
    case OPR_MEM_BP_DI_OFF8:
    case OPR_MEM_SI_OFF8:
    case OPR_MEM_DI_OFF8:
    case OPR_MEM_BP_OFF8:
    case OPR_MEM_BX_OFF8:
        if (off.u8 != other.off.u8) return INS_MATCH_DIFF;
        else break;
    case OPR_MEM_OFF16:
    case OPR_MEM_BX_SI_OFF16:
    case OPR_MEM_BX_DI_OFF16:
    case OPR_MEM_BP_SI_OFF16:
    case OPR_MEM_BP_DI_OFF16:
    case OPR_MEM_SI_OFF16:
    case OPR_MEM_DI_OFF16:
    case OPR_MEM_BP_OFF16:
    case OPR_MEM_BX_OFF16:
        if (off.u16 != other.off.u16) return INS_MATCH_DIFF;
        else break;
    case OPR_IMM8:
        if (imm.u8 != other.imm.u8) return INS_MATCH_DIFF;
        else break;
    case OPR_IMM16:
        if (imm.u16 != other.imm.u16) return INS_MATCH_DIFF;
        else break;
    case OPR_IMM32:
        if (imm.u32 != other.imm.u32) return INS_MATCH_DIFF;
        else break;
    default:
        break;
    }
    return INS_MATCH_FULL;
}

std::string Instruction::toString() const {
    ostringstream str;
    // output chain prefix if present
    if (prefix > PRF_SEG_DS)
        str << PRF_NAME[prefix] << " ";
    
    // output instruction name
    // conditional jumps, lookup specific jump name
    if (iclass == INS_JMP && opcodeIsConditionalJump(opcode)) { 
        Byte idx = opcode - OP_JO_Jb;
        assert(idx < 16);
        str << JMP_NAME[idx];
    }
    // otherwise just output name corresponding to instruction class
    else {
        str << INS_NAME[iclass];
    }

    // output operands
    if (op1.type != OPR_NONE) {
        str << " ";
        // show size prefix if not implicit from operands
        if (operandIsMem(op1.type) && operandIsImmediate(op2.type)) {
            OperandSize immSize = op1.size;
            if (immSize == OPRSZ_UNK) immSize = op2.size;
            switch(immSize) {
            case OPRSZ_BYTE:  str << "byte ";  break;
            case OPRSZ_WORD:  str << "word ";  break;
            case OPRSZ_DWORD: str << "dword "; break;
            default:
                throw CpuError("unexpected immediate operand size: "s + OPR_SIZE_ID[immSize]);
            }
        }
        // segment override prefix if present
        if (prefix > PRF_NONE && prefix < PRF_CHAIN_REPNZ)
            str << PRF_NAME[prefix];
        str << op1.toString();
    }
    if (op2.type != OPR_NONE) {
        str << ", " << op2.toString();
    }

    return str.str();
}

InstructionMatch Instruction::match(const Instruction &other) {
    if (prefix != other.prefix || opcode != other.opcode || iclass != other.iclass || iclass != other.iclass)
        return INS_MATCH_MISMATCH;

    const InstructionMatch 
        op1match = op1.match(other.op1),
        op2match = op2.match(other.op2);
        
    if (op1match == INS_MATCH_MISMATCH || op2match == INS_MATCH_MISMATCH)
        return INS_MATCH_MISMATCH; // either operand mismatch -> instruction mismatch
    else if (op1match != INS_MATCH_FULL && op2match != INS_MATCH_FULL)
        return INS_MATCH_MISMATCH; // both operands non-full match -> instruction mismatch
    else if (op1match == INS_MATCH_DIFF)
        return INS_MATCH_DIFFOP1; // difference on operand 1
    else if (op2match == INS_MATCH_DIFF)
        return INS_MATCH_DIFFOP2; // difference on operand 2
    else
        return INS_MATCH_FULL;
}

// lookup table for converting modrm mod and mem values into OperandType
static const OperandType MODRM_BYTE_MEM_OP[4][8] = {
    OPR_MEM_BX_SI,       OPR_MEM_BX_DI,       OPR_MEM_BP_SI,       OPR_MEM_BP_DI,       OPR_MEM_SI,       OPR_MEM_DI,       OPR_MEM_OFF16,    OPR_MEM_BX,       // mod 00 (no displacement)
    OPR_MEM_BX_SI_OFF8,  OPR_MEM_BX_DI_OFF8,  OPR_MEM_BP_SI_OFF8,  OPR_MEM_BP_DI_OFF8,  OPR_MEM_SI_OFF8,  OPR_MEM_DI_OFF8,  OPR_MEM_BP_OFF8,  OPR_MEM_BX_OFF8,  // mod 01 (8bit displacement)
    OPR_MEM_BX_SI_OFF16, OPR_MEM_BX_DI_OFF16, OPR_MEM_BP_SI_OFF16, OPR_MEM_BP_DI_OFF16, OPR_MEM_SI_OFF16, OPR_MEM_DI_OFF16, OPR_MEM_BP_OFF16, OPR_MEM_BX_OFF16, // mod 10 (16bit displacement)
    OPR_REG_AL,          OPR_REG_CL,          OPR_REG_DL,          OPR_REG_BL,          OPR_REG_AH,       OPR_REG_CH,       OPR_REG_DH,       OPR_REG_BH,       // mod 11 (register)
};

static const OperandType MODRM_WORD_MEM_OP[4][8] = {
    OPR_MEM_BX_SI,       OPR_MEM_BX_DI,       OPR_MEM_BP_SI,       OPR_MEM_BP_DI,       OPR_MEM_SI,       OPR_MEM_DI,       OPR_MEM_OFF16,    OPR_MEM_BX,       // mod 00 (no displacement)
    OPR_MEM_BX_SI_OFF8,  OPR_MEM_BX_DI_OFF8,  OPR_MEM_BP_SI_OFF8,  OPR_MEM_BP_DI_OFF8,  OPR_MEM_SI_OFF8,  OPR_MEM_DI_OFF8,  OPR_MEM_BP_OFF8,  OPR_MEM_BX_OFF8,  // mod 01 (8bit displacement)
    OPR_MEM_BX_SI_OFF16, OPR_MEM_BX_DI_OFF16, OPR_MEM_BP_SI_OFF16, OPR_MEM_BP_DI_OFF16, OPR_MEM_SI_OFF16, OPR_MEM_DI_OFF16, OPR_MEM_BP_OFF16, OPR_MEM_BX_OFF16, // mod 10 (16bit displacement)
    OPR_REG_AX,          OPR_REG_CX,          OPR_REG_DX,          OPR_REG_BX,          OPR_REG_SP,       OPR_REG_BP,       OPR_REG_SI,       OPR_REG_DI,       // mod 11 (register)
};

static const OperandType MODRM_MEM_OP[4][8] = {
    OPR_MEM_BX_SI,       OPR_MEM_BX_DI,       OPR_MEM_BP_SI,       OPR_MEM_BP_DI,       OPR_MEM_SI,       OPR_MEM_DI,       OPR_MEM_OFF16,    OPR_MEM_BX,       // mod 00 (no displacement)
    OPR_MEM_BX_SI_OFF8,  OPR_MEM_BX_DI_OFF8,  OPR_MEM_BP_SI_OFF8,  OPR_MEM_BP_DI_OFF8,  OPR_MEM_SI_OFF8,  OPR_MEM_DI_OFF8,  OPR_MEM_BP_OFF8,  OPR_MEM_BX_OFF8,  // mod 01 (8bit displacement)
    OPR_MEM_BX_SI_OFF16, OPR_MEM_BX_DI_OFF16, OPR_MEM_BP_SI_OFF16, OPR_MEM_BP_DI_OFF16, OPR_MEM_SI_OFF16, OPR_MEM_DI_OFF16, OPR_MEM_BP_OFF16, OPR_MEM_BX_OFF16, // mod 10 (16bit displacement)
    OPR_ERR,             OPR_ERR,             OPR_ERR,             OPR_ERR,             OPR_ERR,          OPR_ERR,          OPR_ERR,          OPR_ERR,          // mod 11 (register)
};

static const OperandType MODRM_BYTE_REG_OP[8] = {
    OPR_REG_AL, OPR_REG_CL, OPR_REG_DL, OPR_REG_BL, OPR_REG_AH, OPR_REG_CH, OPR_REG_DH, OPR_REG_BH,
};

static const OperandType MODRM_WORD_REG_OP[8] = {
    OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI,
};

static const OperandType MODRM_SEGREG_OP[8] = {
    OPR_REG_ES, OPR_REG_CS, OPR_REG_SS, OPR_REG_DS, OPR_ERR, OPR_ERR, OPR_ERR, OPR_ERR,
};

OperandType Instruction::getModrmOperand(const Byte modrm, const ModrmOperand op) {
    OperandType ret = OPR_NONE;
    const Byte 
        modVal = modrm_mod(modrm) >> MODRM_MOD_SHIFT,
        regVal = modrm_reg(modrm) >> MODRM_REG_SHIFT,
        mem = modrm_mem(modrm);

    switch (op) {
    case MODRM_NONE: break;
    case MODRM_Eb: ret = MODRM_BYTE_MEM_OP[modVal][mem]; break;
    case MODRM_Gb: ret = MODRM_BYTE_REG_OP[regVal]; break;
    case MODRM_Ib: ret = OPR_IMM8; break;
    case MODRM_Ev: ret = MODRM_WORD_MEM_OP[modVal][mem]; break;    
    case MODRM_Gv: ret = MODRM_WORD_REG_OP[regVal]; break;
    case MODRM_Iv: ret = OPR_IMM16; break;
    case MODRM_Sw: ret = MODRM_SEGREG_OP[regVal]; break;
    case MODRM_M:  
    case MODRM_Mp: ret = MODRM_MEM_OP[modVal][mem]; break;
    case MODRM_1:  ret = OPR_IMM1; break;
    case MODRM_CL: ret = OPR_REG_CL; break;
    default: 
        return OPR_ERR;
    }
    return ret;
}

void Instruction::loadOperand(Operand &op) {
    Word wordVal;
    Byte byteVal;
    switch (op.type) {
    case OPR_NONE:
    case OPR_REG_AX:
    case OPR_REG_AL:
    case OPR_REG_AH:
    case OPR_REG_BX:
    case OPR_REG_BL:
    case OPR_REG_BH:
    case OPR_REG_CX:
    case OPR_REG_CL:
    case OPR_REG_CH:
    case OPR_REG_DX:
    case OPR_REG_DL:
    case OPR_REG_DH:
    case OPR_REG_SI:
    case OPR_REG_DI:
    case OPR_REG_BP:
    case OPR_REG_SP:
    case OPR_REG_CS:
    case OPR_REG_DS:
    case OPR_REG_ES:
    case OPR_REG_SS: 
    case OPR_MEM_BX_SI:
    case OPR_MEM_BX_DI:
    case OPR_MEM_BP_SI:
    case OPR_MEM_BP_DI:
    case OPR_MEM_SI:
    case OPR_MEM_DI:
    case OPR_MEM_BX:
        // register and memory operands with no displacement 
        DEBUG("direct or no operand, offset = immval = 0");
        op.off.s16 = 0; 
        op.imm.u32 = 0; 
        break;
    case OPR_MEM_OFF8:
    case OPR_MEM_BX_SI_OFF8:
    case OPR_MEM_BX_DI_OFF8:
    case OPR_MEM_BP_SI_OFF8:
    case OPR_MEM_BP_DI_OFF8:
    case OPR_MEM_SI_OFF8:
    case OPR_MEM_DI_OFF8:
    case OPR_MEM_BP_OFF8:
    case OPR_MEM_BX_OFF8:
        // memory operands with 8bit displacement
        // TODO: handle endianness correctly
        op.off.s16 = *reinterpret_cast<const SByte*>(data++);
        op.imm.u32 = 0;
        length++;
        DEBUG("off8 operand, value = "s + hexVal(op.off.s16) + ", length = " + to_string(length));
        break;
    case OPR_MEM_OFF16:
    case OPR_MEM_BX_SI_OFF16:
    case OPR_MEM_BX_DI_OFF16:
    case OPR_MEM_BP_SI_OFF16:
    case OPR_MEM_BP_DI_OFF16:
    case OPR_MEM_SI_OFF16:
    case OPR_MEM_DI_OFF16:
    case OPR_MEM_BP_OFF16:
    case OPR_MEM_BX_OFF16:
        // memory operands with 8bit displacement
        // TODO: handle endianness correctly
        op.off.s16 = *reinterpret_cast<const SWord*>(data);
        op.imm.u32 = 0;
        data += 2;
        length += 2;
        DEBUG("off16 operand, value = "s + hexVal(op.off.s16) + ", length = " + to_string(length));
        break;    
    case OPR_IMM0:
        op.off.s16 = 0;
        op.imm.u32 = 0;
        DEBUG("imm0 operand");
        break;
    case OPR_IMM1:
        op.off.s16 = 0;
        op.imm.u32 = 1;
        DEBUG("imm1 operand");
        break;    
    case OPR_IMM8:
        length += 1;
        op.off.s16 = 0;
        byteVal = *reinterpret_cast<const Byte*>(data++);
        DEBUG("imm8 operand = "s + hexVal(byteVal) + ", length = " + to_string(length));
        // for calls and jumps apply correction to the operand value - it is a signed offset relative to the instruction end
        if (iclass == INS_CALL || iclass == INS_JMP) {
            op.imm.s16 = addr.offset + length;
            op.imm.s16 += static_cast<SByte>(byteVal);
            DEBUG("applied operand correction: " + hexVal(addr.offset) + "+" + hexVal(length) + signedHexVal(static_cast<SByte>(byteVal),true) + " = " + hexVal(op.imm.u16));
            op.type = OPR_IMM16; // loaded as 8bit but display as 16
        }
        else op.imm.u8 = byteVal;
        break;    
    case OPR_IMM16:
        length += 2;
        DEBUG("imm16 operand = "s + hexVal(wordVal) + ", length = " + to_string(length));
        op.off.s16 = 0;
        wordVal = *reinterpret_cast<const Word*>(data);
        if (iclass == INS_CALL || iclass == INS_JMP) {
            op.imm.s16 = addr.offset + length;
            op.imm.s16 += static_cast<SWord>(wordVal);
        }
        else op.imm.u16 = wordVal;
        data += 2;
        break;
    case OPR_IMM32:
        op.off.s16 = 0;
        op.imm.u32 =  *reinterpret_cast<const DWord*>(data);
        data += 4;
        length += 4;
        DEBUG("imm32 operand = "s + hexVal(op.imm.u32) + ", length = " + to_string(length));
        break;
    default:
        throw CpuError("Invalid operand: "s + hexVal((Byte)op.type));
    }
}