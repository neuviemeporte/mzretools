#include "dos/instruction.h"
#include "dos/opcodes.h"
#include "dos/error.h"
#include "dos/util.h"
#include "dos/output.h"

#include <sstream>
#include <cstring>

using namespace std;

// TODO: handle invalid opcodes gracefully, don't throw/assert

#define DEBUG(msg) output(msg, LOG_CPU, LOG_DEBUG)
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

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

// maps non-group opcodes to an instruction class
static const InstructionClass OPCODE_CLASS[] = {
// 0           1           2           3           4           5           6           7           8           9           A             B           C           D           E           F
INS_ADD,    INS_ADD,    INS_ADD,    INS_ADD,    INS_ADD,    INS_ADD,    INS_PUSH,   INS_POP,    INS_OR,     INS_OR,     INS_OR,       INS_OR,     INS_OR,     INS_OR,     INS_PUSH,   INS_ERR,    // 0
INS_ADC,    INS_ADC,    INS_ADC,    INS_ADC,    INS_ADC,    INS_ADC,    INS_PUSH,   INS_POP,    INS_SBB,    INS_SBB,    INS_SBB,      INS_SBB,    INS_SBB,    INS_SBB,    INS_PUSH,   INS_POP,    // 1
INS_AND,    INS_AND,    INS_AND,    INS_AND,    INS_AND,    INS_AND,    INS_ERR,    INS_DAA,    INS_SUB,    INS_SUB,    INS_SUB,      INS_SUB,    INS_SUB,    INS_SUB,    INS_ERR,    INS_DAS,    // 2
INS_XOR,    INS_XOR,    INS_XOR,    INS_XOR,    INS_XOR,    INS_XOR,    INS_ERR,    INS_AAA,    INS_CMP,    INS_CMP,    INS_CMP,      INS_CMP,    INS_CMP,    INS_CMP,    INS_ERR,    INS_AAS,    // 3
INS_INC,    INS_INC,    INS_INC,    INS_INC,    INS_INC,    INS_INC,    INS_INC,    INS_INC,    INS_DEC,    INS_DEC,    INS_DEC,      INS_DEC,    INS_DEC,    INS_DEC,    INS_DEC,    INS_DEC,    // 4
INS_PUSH,   INS_PUSH,   INS_PUSH,   INS_PUSH,   INS_PUSH,   INS_PUSH,   INS_PUSH,   INS_PUSH,   INS_POP,    INS_POP,    INS_POP,      INS_POP,    INS_POP,    INS_POP,    INS_POP,    INS_POP,    // 5
INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,      INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    // 6
INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF,   INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, INS_JMP_IF, // 7
INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_TEST,   INS_TEST,   INS_XCHG,   INS_XCHG,   INS_MOV,    INS_MOV,    INS_MOV,      INS_MOV,    INS_MOV,    INS_LEA,    INS_MOV,    INS_POP,    // 8
INS_NOP,    INS_XCHG,   INS_XCHG,   INS_XCHG,   INS_XCHG,   INS_XCHG,   INS_XCHG,   INS_XCHG,   INS_CBW,    INS_CWD,    INS_CALL_FAR, INS_WAIT,   INS_PUSHF,  INS_POPF,   INS_SAHF,   INS_LAHF,   // 9
INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOVSB,  INS_MOVSW,  INS_CMPSB,  INS_CMPSW,  INS_TEST,   INS_TEST,   INS_STOSB,    INS_STOSW,  INS_LODSB,  INS_LODSW,  INS_SCASB,  INS_SCASW,  // A
INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,      INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    INS_MOV,    // B
INS_ERR,    INS_ERR,    INS_RET,    INS_RET,    INS_LES,    INS_LDS,    INS_MOV,    INS_MOV,    INS_ERR,    INS_ERR,    INS_RETF,     INS_RETF,   INS_INT3,   INS_INT,    INS_INTO,   INS_IRET,   // C
INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_AAM,    INS_AAD,    INS_ERR,    INS_XLAT,   INS_ERR,    INS_ERR,    INS_ERR,      INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    INS_ERR,    // D
INS_LOOPNZ, INS_LOOPZ,  INS_LOOP,   INS_JMP_IF, INS_IN,     INS_IN,     INS_OUT,    INS_OUT,    INS_CALL,   INS_JMP,    INS_JMP_FAR,  INS_JMP,    INS_IN,     INS_IN,     INS_OUT,    INS_OUT,    // E
INS_LOCK,   INS_ERR,    INS_REPNZ,  INS_REPZ,   INS_HLT,    INS_CMC,    INS_ERR,    INS_ERR,    INS_CLC,    INS_STC,    INS_CLI,      INS_STI,    INS_CLD,    INS_STD,    INS_ERR,    INS_ERR,    // F
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
//   0         1           2              3              4           5           6           7           8           9           A           B           C           D           E           F
OPR_ERR,    OPR_ERR,    OPR_ERR,       OPR_ERR,       OPR_REG_AL, OPR_REG_AX, OPR_REG_ES, OPR_REG_ES, OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_REG_CS, OPR_ERR,    // 0
OPR_ERR,    OPR_ERR,    OPR_ERR,       OPR_ERR,       OPR_REG_AL, OPR_REG_AX, OPR_REG_SS, OPR_REG_SS, OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_REG_DS, OPR_REG_DS, // 1
OPR_ERR,    OPR_ERR,    OPR_ERR,       OPR_ERR,       OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   // 2
OPR_ERR,    OPR_ERR,    OPR_ERR,       OPR_ERR,       OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   // 3
OPR_REG_AX, OPR_REG_CX, OPR_REG_DX,    OPR_REG_BX,    OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // 4
OPR_REG_AX, OPR_REG_CX, OPR_REG_DX,    OPR_REG_BX,    OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // 5
OPR_ERR,    OPR_ERR,    OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 6
OPR_IMM8,   OPR_IMM8,   OPR_IMM8,      OPR_IMM8,      OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   // 7
OPR_ERR,    OPR_ERR,    OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 8
OPR_NONE,   OPR_REG_CX, OPR_REG_DX,    OPR_REG_BX,    OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_NONE,   OPR_NONE,   OPR_IMM32,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 9
OPR_REG_AL, OPR_REG_AX, OPR_MEM_OFF16, OPR_MEM_OFF16, OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_REG_AL, OPR_REG_AX, OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // A
OPR_REG_AL, OPR_REG_CL, OPR_REG_DL,    OPR_REG_BL,    OPR_REG_AH, OPR_REG_CH, OPR_REG_DH, OPR_REG_BH, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // B
OPR_ERR,    OPR_ERR,    OPR_IMM16,     OPR_NONE,      OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_IMM8,   OPR_NONE,   OPR_NONE,   // C
OPR_ERR,    OPR_ERR,    OPR_ERR,       OPR_ERR,       OPR_IMM0,   OPR_IMM0,   OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // D
OPR_IMM8,   OPR_IMM8,   OPR_IMM8,      OPR_IMM8,      OPR_REG_AL, OPR_REG_AX, OPR_IMM8,   OPR_IMM8,   OPR_IMM16,  OPR_IMM16,  OPR_IMM32,  OPR_IMM8,   OPR_REG_AL, OPR_REG_AX, OPR_REG_DX, OPR_REG_DX, // E
OPR_NONE,   OPR_NONE,   OPR_NONE,      OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    // F
};

// maps non-modrm opcodes into their first operand's type
static const OperandType OP2_TYPE[] = {
//   0            1              2           3           4           5           6           7           8          9          A          B          C           D           E           F
OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_ERR,    // 0
OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   // 1
OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_ERR,    OPR_NONE,   // 2
OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_ERR,    OPR_NONE,   // 3
OPR_NONE,      OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 4
OPR_NONE,      OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 5
OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 6
OPR_NONE,      OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 7
OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 8
OPR_NONE,      OPR_REG_AX,    OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 9
OPR_MEM_OFF16, OPR_MEM_OFF16, OPR_REG_AL, OPR_REG_AX, OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_IMM8,  OPR_IMM16, OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // A
OPR_IMM8,      OPR_IMM8,      OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM16, OPR_IMM16, OPR_IMM16, OPR_IMM16, OPR_IMM16,  OPR_IMM16,  OPR_IMM16,  OPR_IMM16,  // B
OPR_ERR,       OPR_ERR,       OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // C
OPR_ERR,       OPR_ERR,       OPR_ERR,    OPR_ERR,    OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // D
OPR_NONE,      OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_IMM8,   OPR_IMM8,   OPR_REG_AL, OPR_REG_AX, OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_REG_DX, OPR_REG_DX, OPR_REG_AL, OPR_REG_AX, // E
OPR_NONE,      OPR_NONE,      OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    // F
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

// convert an operand type with a memory offset or an immediate value to a word-sized equivalent, 
// useful in fuzzy comparisons
OperandType operandTypeToWord(const OperandType ot) {
    OperandType ret = OPR_ERR;
    switch (ot) {
    case OPR_MEM_OFF8:
        ret = OPR_MEM_OFF16;
        break;
    case OPR_MEM_BX_SI_OFF8:
        ret = OPR_MEM_BX_SI_OFF16;
        break;
    case OPR_MEM_BX_DI_OFF8:
        ret = OPR_MEM_BX_DI_OFF16;
        break;
    case OPR_MEM_BP_SI_OFF8:
        ret = OPR_MEM_BP_SI_OFF16;
        break;
    case OPR_MEM_BP_DI_OFF8:
        ret = OPR_MEM_BP_DI_OFF16;
        break;
    case OPR_MEM_SI_OFF8:
        ret = OPR_MEM_SI_OFF16;
        break;
    case OPR_MEM_DI_OFF8:
        ret = OPR_MEM_DI_OFF16;
        break;
    case OPR_MEM_BP_OFF8:
        ret = OPR_MEM_BP_OFF16;
        break;
    case OPR_MEM_BX_OFF8:
        ret = OPR_MEM_BX_OFF16;
        break;
    case OPR_IMM8:
        ret = OPR_IMM16;
        break;
    default:
        ret = ot;
        break;
    }
    return ret;
}

Register defaultMemSegment(const OperandType ot) {
    if (!operandIsMem(ot)) return REG_NONE;
    Register ret = REG_NONE;
    switch (ot) {
    case OPR_MEM_BP_SI:
    case OPR_MEM_BP_DI:      
    case OPR_MEM_BP_SI_OFF8: 
    case OPR_MEM_BP_DI_OFF8: 
    case OPR_MEM_BP_OFF8:    
    case OPR_MEM_BP_SI_OFF16:
    case OPR_MEM_BP_DI_OFF16:
    case OPR_MEM_BP_OFF16:    ret = REG_SS; break;
    default:                  ret = REG_DS; break;
    }
    return ret;
}

Instruction::Instruction() : addr{}, prefix(PRF_NONE), opcode(OP_INVALID), iclass(INS_ERR), length(0), data(nullptr) {
}

Instruction::Instruction(const Address &addr, const Byte *data) : addr{addr}, prefix(PRF_NONE), opcode(OP_INVALID), iclass(INS_ERR), length(0) {
    load(data);
}

void Instruction::load(const Byte *data)  {
    this->data = data;
    opcode = *data++;
    length++;
    // in case of a chain opcode, use it to set an appropriate prefix value and replace the opcode with the subsequent instruction
    // TODO: support LOCK, other prefix-like opcodes?
    if (opcode == OP_REPZ || opcode == OP_REPNZ) { 
        prefix = static_cast<InstructionPrefix>(opcode - OP_REPNZ + PRF_CHAIN_REPNZ); // convert opcode to instruction prefix enum
        // TODO: guard against memory overflow
        opcode = *data++;
        length++;
        DEBUG("Found chain prefix "s + INS_PRF_ID[prefix] + ", length = " + to_string(length));
    }
    // likewise in case of a segment ovverride prefix, set instruction prefix value and get next opcode
    else if (opcodeIsSegmentPrefix(opcode)) {
        prefix = static_cast<InstructionPrefix>(((opcode - OP_PREFIX_ES) / 8) + PRF_SEG_ES); // convert opcode to instruction prefix enum, the segment prefix opcode values differ by 8
        opcode = *data++;
        length++;
        DEBUG("Found segment prefix "s + INS_PRF_ID[prefix] + ", length = " + to_string(length));
    }

    // regular instruction opcode
    if (!opcodeIsModrm(opcode)) {
        iclass = instr_class(opcode);
        // get type of operands
        op1.type = OP1_TYPE[opcode];
        op2.type = OP2_TYPE[opcode];
        DEBUG("regular opcode "s + opcodeName(opcode) + ", operand types: op1 = "s + OPR_TYPE_ID[op1.type] + ", op2 = " + OPR_TYPE_ID[op2.type] 
            + ", class " + INS_CLASS_ID[iclass]);        
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
        DEBUG("modrm opcode "s + opcodeName(opcode) + ", modrm = " + hexVal(modrm) + ", operand types: op1 = "s + MODRM_OPR_ID[modop1] + ", op2 = " + MODRM_OPR_ID[modop2] 
            + ", class " + INS_CLASS_ID[iclass]);
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

    // TODO: handle gracefully, throw
    assert(op1.type != OPR_ERR && op2.type != OPR_ERR);
    DEBUG("generalized operands, op1: type = "s + OPR_TYPE_ID[op1.type] + ", size = " + OPR_SIZE_ID[op1.size] + ", op2: type = " + OPR_TYPE_ID[op2.type] + ", size = " + OPR_SIZE_ID[op2.size]);
    // load immediate values if present
    Size immSize = loadImmediate(op1, data);
    data += immSize;
    length += immSize;
    immSize = loadImmediate(op2, data);
    data += immSize;
    length += immSize;
    DEBUG("Instruction @" + addr.toString() + ": " + toString() + ", length = " + to_string(length));
}

// calculate an absolute offset from an offset that is relative to this instruction's end, based on the immediate operand 
// - this is useful for branch instructions like call and the various jumps, whose operand is the number of bytes to jump forward or back, 
// relative to the byte past the instruction
Word Instruction::absoluteOffset() const {
    return addr.offset + length + relativeOffset();
}

// return the branch destination address 
Address Instruction::destinationAddress() const {
    return { addr.segment, absoluteOffset() };
}

SWord Instruction::relativeOffset() const {
    switch (op1.type) {
    case OPR_IMM8: return static_cast<SByte>(op1.immval.u8);
    case OPR_IMM16: return static_cast<SWord>(op1.immval.u16);
    }
    return 0;
}


// lookup table for which instructions modify their first operand
// 0: doesn't modify any regs, 1: modifies 1st operand, 2: modifies both operands, 3: special case, implicit registers
// TODO: ret accepts operand?
// TODO: doesn't include flags register
// TODO: put values in enum
static const int OPERAND_MODIFIED[] = {
    //INS_ERR   INS_ADD      INS_PUSH    INS_POP    INS_OR     INS_ADC    INS_SBB     INS_AND    INS_DAA    INS_SUB   INS_DAS       INS_XOR   INS_AAA    INS_CMP   INS_AAS    INS_INC   INS_DEC
    0,          1,           3,          3,         1,         1,         1,          1,         3,         1,        3,            1,        3,         0,        3,         1,        1,
    //INS_JMP   INS_JMP_IF  INS_JMP_FAR  INS_TEST    INS_XCHG   INS_MOV    INS_LEA    INS_NOP     INS_CBW    INS_CWD    INS_CALL  INS_CALL_FAR  INS_WAIT  INS_PUSHF  INS_POPF  INS_SAHF   INS_LAHF  INS_MOVSB
    0,          0,           0,           3,          2,         1,         1,         0,          3,         3,         0,        0,            0,        3,         3,        3,         3,        2, 
    //INS_MOVSW INS_CMPSB    INS_CMPSW   INS_STOSB  INS_STOSW  INS_LODSB  INS_LODSW   INS_SCASB  INS_SCASW  INS_RET   INS_LES       INS_LDS   INS_RETF   INS_INT   INS_INT3   INS_INTO   INS_IRET  INS_AAM
    3,          3,           3,          3,         3,         3,         0,          3,         3,         0,        3,            3,        0,         3,        3,          3,         0,        3, 
    //INS_AAD   INS_XLAT     INS_LOOPNZ  INS_LOOPZ  INS_LOOP   INS_IN     INS_OUT     INS_LOCK   INS_REPNZ  INS_REPZ  INS_HLT       INS_CMC   INS_CLC    INS_STC   INS_CLI    INS_STI   INS_CLD
    3,          3,           3,          3,         3,         1,         0,          0,         0,         0,        0,            3,        3,         3,        3,         3,        3, 
    //INS_STD   INS_ROL      INS_ROR     INS_RCL    INS_RCR    INS_SHL    INS_SHR     INS_SAR    INS_NOT    INS_NEG   INS_MUL       INS_IMUL  INS_DIV    INS_IDIV
    3,          1,           1,          3,         3,         1,         1,          0,         1,         1,        0,            3,        3,         3,
};
static_assert(ARRAY_SIZE(OPERAND_MODIFIED) == ARRAY_SIZE(INS_CLASS_ID));

// TODO: basically all special cases, refactor this
vector<Register> Instruction::touchedRegs() const {
    const int touch = OPERAND_MODIFIED[iclass];
    vector<Register> ret;
    if (touch == 0) return { REG_NONE };
    else if (touch == 1) ret = { op1.regId() }; 
    else if (touch == 2) ret = { op1.regId(), op2.regId() };
    else switch(iclass) {
    case INS_AAA:
    case INS_AAD:
    case INS_AAM:
    case INS_AAS: ret = { REG_AL, REG_AH }; break;
    case INS_CALL:
    case INS_CALL_FAR:
    case INS_INT: ret = { REG_AX, REG_BX, REG_CX, REG_DX, REG_SI, REG_DI, REG_FLAGS }; break;
    case INS_CBW:
    case INS_LAHF: ret = { REG_AH }; break;
    case INS_CMPSB:
    case INS_CMPSW:
    case INS_MOVSB:
    case INS_MOVSW: ret = { REG_SI, REG_DI }; break;
    case INS_CWD: ret = { REG_DX }; break;
    case INS_DAA:
    case INS_DAS: ret = { REG_AL }; break;
    case INS_DIV:
    case INS_IDIV:
        if (op1.size == OPRSZ_BYTE) return { REG_AL, REG_AH };
        else return { REG_AX, REG_DX };
        break;
    case INS_MUL:
    case INS_IMUL:
        if (op1.size == OPRSZ_BYTE) return { REG_AX };
        else return { REG_AX, REG_DX };
        break;
    case INS_LDS: ret = { op1.regId(), REG_DS }; break;
    case INS_LES: ret = { op1.regId(), REG_ES }; break;
    case INS_LODSB: ret = { REG_AL, REG_SI }; break;
    case INS_LODSW: ret = { REG_AX, REG_SI }; break;
    case INS_LOOP:
    case INS_LOOPZ:
    case INS_LOOPNZ: ret = { REG_CX }; break;
    case INS_POP: ret = { op1.regId(), REG_SP }; break;
    case INS_POPF: ret = { REG_FLAGS, REG_SP }; break;
    case INS_PUSH:
    case INS_PUSHF: ret = { REG_SP }; break;
    case INS_RCL:
    case INS_RCR: ret = { op1.regId(), REG_FLAGS }; break;
    case INS_CMC:
    case INS_CLC:
    case INS_CLI:
    case INS_STI:
    case INS_STC:
    case INS_CLD:
    case INS_STD:
    case INS_TEST:
    case INS_SAHF: ret = { REG_FLAGS }; break;
    case INS_SCASB: 
    case INS_SCASW: ret = { REG_DI, REG_FLAGS }; break;
    case INS_STOSW:
    case INS_STOSB: ret = { REG_DI };
    case INS_XLAT: ret = { REG_AL };
        break;
    default:
        throw CpuError("Unknown instruction class for touched reg: "s + INS_CLASS_ID[iclass]);
    }
    if (prefix == PRF_CHAIN_REPNZ || prefix == PRF_CHAIN_REPZ) 
        ret.push_back(REG_CX);
    return ret;
}

SOffset Instruction::memOffset() const {
    // check if the instruction has a mem operand
    const Operand *op = memOperand();
    if (!op) return 0;

    SOffset ret = 0;
    if (op->type == OPR_MEM_OFF16) {
        ret = op->immval.u16;
    }
    else if (operandIsMemWithByteOffset(op->type)) {
        SByte offset = static_cast<SByte>(op->immval.u8);
        ret = offset;
    }
    else if (operandIsMemWithWordOffset(op->type)) {
        SWord offset = static_cast<SWord>(op->immval.u16);
        ret = offset;
    }

    return ret;
}

Register Instruction::memSegmentId() const {
    // check if the instruction has a mem operand
    const Operand *op = memOperand();
    if (!op) return REG_NONE;
    // segment override prefix is present, use it
    if (prefixIsSegment(prefix)) return prefixRegId(prefix);
    // otherwise use the default segment based on the operand type
    return defaultMemSegment(op->type);
}

static const char* INS_NAME[] = {
    "???", "add", "push", "pop", "or", "adc", "sbb", "and", "daa", "sub", "das", "xor", "aaa", "cmp", "aas", "inc", "dec", "jmp", "???", "jmp far", "test", "xchg", "mov", "lea", "nop", "cbw", "cwd",
    "call", "call far", "wait", "pushf", "popf", "sahf", "lahf", "movsb", "movsw", "cmpsb", "cmpsw", "stosb", "stosw", "lodsb", "lodsw", "scasb", "scasw", "ret", "les", "lds", "retf", "int",
    "int3", "into", "iret", "aam", "aad", "xlat", "loopnz", "loopz", "loop", "in", "out", "lock", "repnz", "repz", "hlt", "cmc", "clc", "stc", "cli", "sti", "cld", "std",
    "rol", "ror", "rcl", "rcr", "shl", "shr", "sar", "not", "neg", "mul", "imul", "div", "idiv"
};
static_assert(ARRAY_SIZE(INS_NAME) == ARRAY_SIZE(INS_CLASS_ID));

static const char* JMP_NAME[] = {
    "jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "ja", "js", "jns", "jpe", "jpo", "jl", "jge", "jle", "jg", "jcxz"
};
static constexpr Size JMP_NAME_COUNT = sizeof(JMP_NAME) / sizeof(JMP_NAME[0]);

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

std::string Instruction::Operand::toString() const {
    ostringstream str;
    if (operandIsReg(type))
        str << OPR_NAME[type];
    else if (operandIsMemNoOffset(type))
        str << "[" << OPR_NAME[type] << "]";
    else if (operandIsMemWithByteOffset(type)) {
        if (type == OPR_MEM_OFF8) str << "[" << hexVal(immval.u8, true, false)  << "]";
        else str << "[" << OPR_NAME[type] << signedHexVal(static_cast<SByte>(immval.u8)) << "]";
    }
    else if (operandIsMemWithWordOffset(type)) {
        if (type == OPR_MEM_OFF16) str << "[" << hexVal(immval.u16, true, false) << "]";
        else str << "[" << OPR_NAME[type] << signedHexVal(static_cast<SWord>(immval.u16)) << "]";
    }
    else if (type == OPR_IMM0 || type == OPR_IMM1)
        str << OPR_NAME[type];
    else if (type == OPR_IMM8)
        str << hexVal(immval.u8, true, false);
    else if (type == OPR_IMM16)
        str << hexVal(immval.u16, true, false);
    else if (type == OPR_IMM32)
        str << hexVal(immval.u32, true, false);

    return str.str();
}

InstructionMatch Instruction::Operand::match(const Operand &other) const {
    if (type != other.type || size != other.size) {
        // accept differing operand types if they are different-sized equivalents
        if (operandTypeToWord(type) == operandTypeToWord(other.type)) {
            // compare offset/immediate values
            if (wordValue() == other.wordValue()) return INS_MATCH_FULL;
            else return INS_MATCH_DIFF;
        }
        else return INS_MATCH_MISMATCH;
    }

    // the rest assumes the types and sizes are the same
    // default value of true covers operands not having an offset/immval component
    bool match = true;
    if (operandIsMemWithByteOffset(type) || type == OPR_IMM8) {
        match = immval.u8 == other.immval.u8;
    }
    else if (operandIsMemWithWordOffset(type) || type == OPR_IMM16) {
        match = immval.u16 == other.immval.u16;
    }
    else if (type == OPR_IMM32) {
        match = immval.u32 == other.immval.u32;
    }
    return match ? INS_MATCH_FULL : INS_MATCH_DIFF;
}

std::string Instruction::toString(const bool extended) const {
    ostringstream str;
    // output chain prefix if present
    if (prefix > PRF_SEG_DS)
        str << PRF_NAME[prefix] << " ";
    
    // output instruction name
    // conditional jumps, lookup specific jump name
    if (iclass == INS_JMP_IF) { 
        Byte idx = opcode - OP_JO_Jb;
        // jcxz special case
        if (idx >= JMP_NAME_COUNT) idx = JMP_NAME_COUNT - 1;
        str << JMP_NAME[idx];
    }
    // otherwise just output name corresponding to instruction class
    else {
        str << INS_NAME[iclass];
    }
    // special extra label for short jump opcode
    if (opcode == OP_JMP_Jb) str << " short";

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
        if (prefix > PRF_NONE && prefix < PRF_CHAIN_REPNZ && operandIsMem(op1.type))
            str << PRF_NAME[prefix];
        // for near branch instructions (call, jump, loop), the immediate relative offset operand is added to the address 
        // of the byte past the current instruction to form an absolute offset
        if (isNearBranch() && operandIsImmediate(op1.type)) {
            const Word aoff = absoluteOffset();
            str << hexVal(aoff, true, false);
            if (extended) {
                SWord roff = relativeOffset();
                if (roff < 0) { roff = -roff; str << " (" << hexVal(roff, true, false) << " up)"; }
                else str << " (" << hexVal(roff, true, false) << " down)";
            }
        }
        else {
            str << op1.toString();
        }
    }
    if (op2.type != OPR_NONE) {
        str << ", ";
        // segment override prefix if present
        if (prefix > PRF_NONE && prefix < PRF_CHAIN_REPNZ && operandIsMem(op2.type))
            str << PRF_NAME[prefix];
        str << op2.toString();
    }

    return str.str();
}

// this is dumb, should implement a proper assembler some day
ByteString Instruction::encoding(const bool abstract) const {
    ByteString ret;
    SOffset off;
    for (int i = 0; i < length; ++i) {
        ret.push_back(static_cast<SWord>(*(data + i)));
    }
    if (abstract && (off = memOffset()) != 0) {
        // TODO: implement replacing of offsets/immediates with negative values in abstract encoding
    }
    return ret;
}

InstructionMatch Instruction::match(const Instruction &other) const {
    // normally we check whether instructions match in their "class", e.g. MOV, not whether they
    // have the same opcode. An exception are conditional jumps, which all belong to class JMP_IF,
    // so the opcode needs to be checked as well, but these do not have alternate encodings beyond these opcodes.
    if (prefix != other.prefix || iclass != other.iclass || (iclass == INS_JMP_IF && opcode != other.opcode))
        return INS_MATCH_MISMATCH;

    // this can only return FULL (everything matches), DIFF (operand type match, different value) or MISMATCH (operand type different)
    const InstructionMatch 
        op1match = op1.match(other.op1), 
        op2match = op2.match(other.op2);

    if (op1match == INS_MATCH_MISMATCH || op2match == INS_MATCH_MISMATCH)
        return INS_MATCH_MISMATCH; // either operand mismatch -> instruction mismatch
    else if (op1match == INS_MATCH_DIFF && op2match == INS_MATCH_DIFF)
        return INS_MATCH_DIFF; // difference on both operands
    else if (op1match == INS_MATCH_DIFF)
        return INS_MATCH_DIFFOP1; // difference on operand 1
    else if (op2match == INS_MATCH_DIFF)
        return INS_MATCH_DIFFOP2; // difference on operand 2
        
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

Size Instruction::loadImmediate(Operand &op, const Byte *data) {
    DWord dwordVal;
    Word wordVal;
    Byte byteVal;
    Size size = 0;

    switch (op.type) {
    case OPR_MEM_OFF8:
    case OPR_MEM_BX_SI_OFF8:
    case OPR_MEM_BX_DI_OFF8:
    case OPR_MEM_BP_SI_OFF8:
    case OPR_MEM_BP_DI_OFF8:
    case OPR_MEM_SI_OFF8:
    case OPR_MEM_DI_OFF8:
    case OPR_MEM_BP_OFF8:
    case OPR_MEM_BX_OFF8:
    case OPR_IMM8:
        size = sizeof(Byte);
        memcpy(&byteVal, data, size);
        op.immval.u8 = byteVal;
        DEBUG("8bit operand, value = "s + hexVal(op.immval.u8) + ", instruction length = " + to_string(length + size));
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
    case OPR_IMM16:
        size = sizeof(Word);
        memcpy(&wordVal, data, size);
        op.immval.u16 = wordVal;
        DEBUG("16bit operand, value = "s + hexVal(op.immval.u16) + ", instruction length = " + to_string(length + size));
        break;    
    case OPR_IMM0:
        op.immval.u32 = 0;
        DEBUG("imm0 operand");
        break;
    case OPR_IMM1:
        op.immval.u32 = 1;
        DEBUG("imm1 operand");
        break;    
    case OPR_IMM32:
        size = sizeof(DWord);
        memcpy(&dwordVal, data, size);
        op.immval.u32 = dwordVal;
        DEBUG("32bit immediate = "s + hexVal(op.immval.u32) + ", instruction length = " + to_string(length + size));
        break;
    default:
        // register and memory operands with no displacement 
        DEBUG("direct or no operand, immval = 0");
        op.immval.u32 = 0; 
        break;
    }
    return size;
}

const Instruction::Operand* Instruction::memOperand() const {
    const Operand *op = nullptr;
    if (operandIsMem(op1.type)) op = &op1;
    else if (operandIsMem(op2.type)) op = &op2;
    return op;
}

Register Instruction::Operand::regId() const {
    static const Register regs[] = { REG_AX, REG_AL, REG_AH, REG_BX, REG_BL, REG_BH, REG_CX, REG_CL, REG_CH, REG_DX, REG_DL, REG_DH, REG_SI, REG_DI, REG_BP, REG_SP, REG_CS, REG_DS, REG_ES, REG_SS };
    if (!operandIsReg(type)) return REG_NONE;
    const int idx = type - OPR_REG_AX;
    assert(idx >= 0 && idx < sizeof(regs) / sizeof(regs[0]));
    return regs[idx];
}

Word Instruction::Operand::wordValue() const {
    Word ret = 0;
    if (size == OPRSZ_WORD) ret = immval.u16;
    else if (size == OPRSZ_BYTE) ret = immval.u8;
    return ret;
}

Register prefixRegId(const InstructionPrefix p) {
                                // PRF_NONE   PRF_ES  PRF_CS  PRF_SS  PRF_DS  PRF_REPNZ PRF_REPZ
    static const Register segs[] = { REG_DS,  REG_ES, REG_CS, REG_SS, REG_DS, REG_NONE, REG_NONE };
    return segs[p];
}

InstructionClass instr_class(const Byte opcode) {
    return OPCODE_CLASS[opcode];
}

const char* instr_class_name(const InstructionClass iclass) {
    return INS_CLASS_ID[iclass];
}