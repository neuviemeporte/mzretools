#include "dos/instruction.h"
#include "dos/opcodes.h"
#include "dos/modrm.h"
#include "dos/error.h"
#include "dos/util.h"

#include <sstream>

using namespace std;

// TODO: handle invalid opcodes gracefully, don't throw/assert

// maps (non-group) opcodes to an instruction class
static const InstructionClass INSTR_CLASS[] = {
//   0         1          2          3         4          5          6         7           8         9         A          B          C          D          E          F
INS_ADD,    INS_ADD,   INS_ADD,   INS_ADD,  INS_ADD,   INS_ADD,   INS_PUSH,  INS_POP,   INS_OR,   INS_OR,   INS_OR,    INS_OR,    INS_OR,    INS_OR,    INS_PUSH,  INS_ERR,   // 0
INS_ADC,    INS_ADC,   INS_ADC,   INS_ADC,  INS_ADC,   INS_ADC,   INS_PUSH,  INS_POP,   INS_SBB,  INS_SBB,  INS_SBB,   INS_SBB,   INS_SBB,   INS_SBB,   INS_PUSH,  INS_POP,   // 1
INS_AND,    INS_AND,   INS_AND,   INS_AND,  INS_AND,   INS_AND,   INS_ERR,   INS_DAA,   INS_SUB,  INS_SUB,  INS_SUB,   INS_SUB,   INS_SUB,   INS_SUB,   INS_ERR,   INS_DAS,   // 2
INS_XOR,    INS_XOR,   INS_XOR,   INS_XOR,  INS_XOR,   INS_XOR,   INS_ERR,   INS_AAA,   INS_CMP,  INS_CMP,  INS_CMP,   INS_CMP,   INS_CMP,   INS_CMP,   INS_ERR,   INS_AAS,   // 3
INS_INC,    INS_INC,   INS_INC,   INS_INC,  INS_INC,   INS_INC,   INS_INC,   INS_INC,   INS_DEC,  INS_DEC,  INS_DEC,   INS_DEC,   INS_DEC,   INS_DEC,   INS_DEC,   INS_DEC,   // 4
INS_PUSH,   INS_PUSH,  INS_PUSH,  INS_PUSH, INS_PUSH,  INS_PUSH,  INS_PUSH,  INS_PUSH,  INS_POP,  INS_POP,  INS_POP,   INS_POP,   INS_POP,   INS_POP,   INS_POP,   INS_POP,   // 5
INS_ERR,    INS_ERR,   INS_ERR,   INS_ERR,  INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,  INS_ERR,  INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   // 6
INS_JMP,    INS_JMP,   INS_JMP,   INS_JMP,  INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,  INS_JMP,  INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,   INS_JMP,   // 7
INS_ERR,    INS_ERR,   INS_ERR,   INS_ERR,  INS_TEST,  INS_TEST,  INS_XCHG,  INS_XCHG,  INS_MOV,  INS_MOV,  INS_MOV,   INS_MOV,   INS_MOV,   INS_LEA,   INS_MOV,   INS_POP,   // 8
INS_NOP,    INS_XCHG,  INS_XCHG,  INS_XCHG, INS_XCHG,  INS_XCHG,  INS_XCHG,  INS_XCHG,  INS_CBW,  INS_CWD,  INS_CALL,  INS_WAIT,  INS_PUSHF, INS_POPF,  INS_SAHF,  INS_LAHF,  // 9
INS_MOV,    INS_MOV,   INS_MOV,   INS_MOV,  INS_MOVSB, INS_MOVSW, INS_CMPSB, INS_CMPSW, INS_TEST, INS_TEST, INS_STOSB, INS_STOSW, INS_LODSB, INS_LODSW, INS_SCASB, INS_SCASW, // A
INS_MOV,    INS_MOV,   INS_MOV,   INS_MOV,  INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,  INS_MOV,  INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,   INS_MOV,   // B
INS_ERR,    INS_ERR,   INS_RET,   INS_RET,  INS_LES,   INS_LDS,   INS_MOV,   INS_MOV,   INS_ERR,  INS_ERR,  INS_RETF,  INS_RETF,  INS_INT,   INS_INT,   INS_INTO,  INS_IRET,  // C
INS_ERR,    INS_ERR,   INS_ERR,   INS_ERR,  INS_AAM,   INS_AAD,   INS_ERR,   INS_XLAT,  INS_ERR,  INS_ERR,  INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   INS_ERR,   // D
INS_LOOPNZ, INS_LOOPZ, INS_LOOP,  INS_JCXZ, INS_IN,    INS_IN,    INS_OUT,   INS_OUT,   INS_CALL, INS_JMP,  INS_JMP,   INS_JMP,   INS_IN,    INS_IN,    INS_OUT,   INS_OUT,   // E
INS_LOCK,   INS_ERR,   INS_REPNZ, INS_REPZ, INS_HLT,   INS_CMC,   INS_ERR,   INS_ERR,   INS_CLC,  INS_STC,  INS_CLI,   INS_STI,   INS_CLD,   INS_STD,   INS_ERR,   INS_ERR,   // F
};

// maps group opcodes to group indexes in the next table
static const int GRP_IDX[0xff] = {
//  0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 0
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 1
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 2
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 3
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 4
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 5
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 6
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 7
    0,    0,    0,    0,    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 8
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // 9
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // A
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // B
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // C
    1,    1,    1,    1,    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // D
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // E
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 2,    3,    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 4,    5,    // F
};

// maps a group index and the GRP value from a modrm byte to an instruction class for a group opcode
static const InstructionClass GRP_CLASS[6][8] = {
    INS_ADD,  INS_OR,  INS_ADC,  INS_SBB,  INS_AND, INS_SUB,  INS_XOR,  INS_CMP, // GRP1
    INS_ROL,  INS_ROR, INS_RCL,  INS_RCR,  INS_SHL, INS_SHR,  INS_ERR,  INS_SAR, // GRP2
    INS_TEST, INS_ERR, INS_NOT,  INS_NEG,  INS_MUL, INS_IMUL, INS_DIV,  INS_IDIV // GRP3a
    INS_TEST, INS_ERR, INS_NOT,  INS_NEG,  INS_MUL, INS_IMUL, INS_DIV,  INS_IDIV // GRP3b
    INS_INC,  INS_DEC, INS_ERR,  INS_ERR,  INS_ERR, INS_ERR,  INS_ERR,  INS_ERR, // GRP4
    INS_INC,  INS_DEC, INS_CALL, INS_CALL, INS_JMP, INS_JMP,  INS_PUSH, INS_ERR, // GRP5
};

// maps non-modrm opcodes into their first operand's type
static const OperandType OP1_TYPE[] = {
//   0         1           2           3           4           5           6           7           8           9           A           B           C           D           E           F
OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_REG_ES, OPR_REG_ES, OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_REG_CS, OPR_ERR,    // 0
OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_REG_SS, OPR_REG_SS, OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_REG_DS, OPR_REG_DS, // 1
OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   // 2
OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_REG_AL, OPR_REG_AX, OPR_ERR,    OPR_NONE,   // 3
OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // 4
OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // 5
OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 6
OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   // 7
OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 8
OPR_NONE,   OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, OPR_NONE,   OPR_NONE,   OPR_IMM32,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 9
OPR_REG_AL, OPR_REG_AX, OPR_OFF8,   OPR_OFF8,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_REG_AL, OPR_REG_AX, OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // A
OPR_REG_AL, OPR_REG_CL, OPR_REG_DL, OPR_REG_BL, OPR_REG_AH, OPR_REG_CH, OPR_REG_DH, OPR_REG_BH, OPR_REG_AX, OPR_REG_CX, OPR_REG_DX, OPR_REG_BX, OPR_REG_SP, OPR_REG_BP, OPR_REG_SI, OPR_REG_DI, // B
OPR_ERR,    OPR_ERR,    OPR_IMM16,  OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_IMM8,   OPR_NONE,   OPR_NONE,   // C
OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_IMM0,   OPR_IMM0,   OPR_ERR,    OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // D
OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_IMM8,   OPR_REG_AL, OPR_REG_AX, OPR_IMM8,   OPR_IMM8,   OPR_IMM16,  OPR_IMM16,  OPR_IMM32,  OPR_IMM8,   OPR_REG_AL, OPR_REG_AX, OPR_REG_DX, OPR_REG_DX, // E
OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    // F
};

// maps non-modrm opcodes into their first operand's type
static const OperandType OP2_TYPE[] = {
//   0       1          2           3           4           5           6           7           8          9          A          B          C           D           E           F
OPR_ERR,  OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_ERR,    // 0
OPR_ERR,  OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   // 1
OPR_ERR,  OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_ERR,    OPR_NONE,   // 2
OPR_ERR,  OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_IMM8,   OPR_IMM16,  OPR_NONE,   OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_IMM8,   OPR_IMM16,  OPR_ERR,    OPR_NONE,   // 3
OPR_NONE, OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 4
OPR_NONE, OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 5
OPR_ERR,  OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 6
OPR_NONE, OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 7
OPR_ERR,  OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // 8
OPR_NONE, OPR_REG_AX,OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_REG_AX, OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // 9
OPR_OFF8, OPR_OFF16, OPR_REG_AL, OPR_REG_AX, OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_IMM8,  OPR_IMM16, OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // A
OPR_OFF8, OPR_OFF8,  OPR_OFF8,   OPR_OFF8,   OPR_OFF8,   OPR_OFF8,   OPR_OFF8,   OPR_OFF8,   OPR_OFF16, OPR_OFF16, OPR_OFF16, OPR_OFF16, OPR_OFF16,  OPR_OFF16,  OPR_OFF16,  OPR_OFF16,  // B
OPR_ERR,  OPR_ERR,   OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,   OPR_ERR,   OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   // C
OPR_ERR,  OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_NONE,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,   OPR_ERR,    OPR_ERR,    OPR_ERR,    OPR_ERR,    // D
OPR_NONE, OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_IMM8,   OPR_IMM8,   OPR_REG_AL, OPR_REG_AX, OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_REG_DX, OPR_REG_DX, OPR_REG_AL, OPR_REG_AX, // E
OPR_NONE, OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,  OPR_NONE,   OPR_NONE,   OPR_ERR,    OPR_ERR,    // F
};

InstructionClass instr_class(const Byte opcode) {
    return INSTR_CLASS[opcode];
}

Instruction::Instruction(const Byte *idata) : data(idata), addr{}, prefix(PRF_NONE), opcode(OP_INVALID), iclass(INS_ERR), length(0) {
    Byte opcode = *data++;
    // in case of a chain opcode, use it to set an appropriate prefix value and replace the opcode with the subsequent instruction
    // TODO: support LOCK, other prefix-like opcodes?
    if (opcode == OP_REPZ || opcode == OP_REPNZ) { 
        prefix = opcode - OP_REPNZ + PRF_CHAIN_REPNZ; // convert opcode to instruction prefix enum
        // TODO: guard against memory overflow
        opcode = *data++;
        length++;
    }
    // likewise in case of a segment ovverride prefix, set instruction prefix value and get next opcode
    else if (opcodeIsSegmentPrefix(opcode)) {
        prefix = ((opcode - OP_PREFIX_ES) / 8) + PRF_SEG_ES; // convert opcode to instruction prefix enum, the segment prefix opcode values differ by 8
        opcode = *data++;
        length++;
    }

    // regular instruction opcode
    if (!opcodeIsModrm(opcode)) {
        iclass = instr_class(opcode);
        // get type of operands
        op1.type = OP1_TYPE[opcode];
        op2.type = OP2_TYPE[opcode];
        assert(op1.type != OPR_ERR && op2.type != OPR_ERR);
        loadOperand(op1, data);
        loadOperand(op2, data);
    }
    // modr/m insruction opcode
    else if (!opcodeIsGroup(opcode)) {
        const Byte modrm = *data++; // load modrm byte
        length++;
        const ModrmOperand 
            modop1 = modrm_op1type(modrm),
            modop2 = modrm_op2type(modrm);
        iclass = instr_class(opcode);
        // convert from messy modrm operand designation to our nice type
        op1.type = getModrmOperand(modrm, modop1);
        op2.type = getModrmOperand(modrm, modop2);
        assert(op1.type != OPR_ERR && op2.type != OPR_ERR);
        loadOperand(op1, data);
        loadOperand(op2, data);
    }
    // group instruction opcode
    else {
        assert(opcodeIsGroup(opcode));
        const Byte modrm = *data++; // load modrm byte
        length++;
        // obtain index of group for instruction class lookup
        const int grpIdx = GRP_IDX[opcode];
        assert(grpIdx >= 0 && grpIdx <= 5);
        Byte grp = modrm_grp(modrm);
        // determine instruction class
        iclass = GRP_CLASS[grpIdx][grp];
        assert(iclass != INS_ERR);
        // the rest is just like a "normal" modrm opcode
        const ModrmOperand 
            modop1 = modrm_op1type(modrm),
            modop2 = modrm_op2type(modrm);
        // convert from messy modrm operand designation to our nice type
        op1.type = getModrmOperand(modrm, modop1);
        op2.type = getModrmOperand(modrm, modop2);
        assert(op1.type != OPR_ERR && op2.type != OPR_ERR);
        loadOperand(op1, data);
        loadOperand(op2, data);
    }

    // don't need it anymore
    data = nullptr; 
}

static const char* INS_NAME[] = {
    "???", "add", "push", "pop", "or", "adc", "sbb", "and", "daa", "sub", "das", "xor", "aaa", "cmp", "aas", "inc", "dec", "jmp", "test", "xchg", "mov", "lea", "nop", "cbw", "cwd",
    "call", "wait", "pushf", "popf", "sahf", "lahf", "movsb", "movsw", "cmpsb", "cmpsw", "stosb", "stosw", "lodsb", "lodsw", "scasb", "scasw", "ret", "les", "lds", "retf", "int",
    "into", "iret", "aam", "aad", "xlat", "loopnz", "loopz", "loop", "jcxz", "in", "out", "lock", "repnz", "repz", "hlt", "cmc", "clc", "stc", "cli", "sti", "cld", "std",
    "rol", "ror", "rcl", "rcr", "shl", "shr", "sar", "not", "neg", "mul", "imul", "div", "idiv"
};

static const char* INS_JMP[] = {
    "jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "ja", "js", "jns", "jpe", "jpo", "jl", "jge", "jle", "jg",
};

static const char* OP_NAME[] = {
    "???", "X", 
    "ax", "al", "ah", "bx", "bl", "bh", "cx", "cl", "ch", "dx", "dl", "dh", "si", "di", "bp", "sp", "cs", "ds", "es", "ss"
    "bx+si", "bx+di", "bp+si", "bp+di", "si", "di", "bx", 
    "d8", "bx+si+", "bx+di+", "bp+si+", "bp+di+", "si+", "di+", "bp+", "bx+",
    "d16", "bx+si+", "bx+di+", "bp+si+", "bp+di+", "si+", "di+", "bp+", "bx+",
    "i0", "i1", "i8", "i16", "i32",
};

std::string Instruction::toString() const {
    ostringstream str;

    // output loop prefix if present
    if (prefix == PRF_CHAIN_REPZ)
        str << "repz ";
    else if (prefix == PRF_CHAIN_REPNZ)
        str << "repnz ";
    
    // output instruction name
    if (iclass == INS_JMP && opcode < OP_GRP1_Eb_Ib) // conditional jumps
        str << INS_JMP[opcode - OP_JO_Jb];
    else
        str << INS_NAME[iclass];
    str << " ";

    // output operand 1
    if (op1.type != OPR_NONE) {
        
    }




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
        mod = modrm_mod(modrm),
        reg = modrm_reg(modrm),
        mem = modrm_mem(modrm);

    switch (op) {
    case MODRM_NONE: break;
    case MODRM_Eb: ret = MODRM_BYTE_MEM_OP[mod][mem]; break;
    case MODRM_Gb: ret = MODRM_BYTE_REG_OP[reg]; break;
    case MODRM_Ib: ret = OPR_IMM8; break;
    case MODRM_Ev: ret = MODRM_WORD_MEM_OP[mod][mem]; break;    
    case MODRM_Gv: ret = MODRM_WORD_REG_OP[reg]; break;
    case MODRM_Iv: ret = OPR_IMM16; break;
    case MODRM_Sw: ret = MODRM_SEGREG_OP[reg]; break;
    case MODRM_M:  
    case MODRM_Mp: ret = MODRM_MEM_OP[mod][mem]; break;
    case MODRM_1:  ret = OPR_IMM1; break;
    case MODRM_CL: ret = OPR_REG_CL; break;
    default: 
        return OPR_ERR;
    }
    return ret;
}

void Instruction::loadOperand(Operand &op) {
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
        op.offset = 0; 
        op.immval = 0; 
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
        op.offset = *reinterpret_cast<const SByte*>(data++);
        op.immval = 0;
        length++;
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
        op.offset = *reinterpret_cast<const SWord*>(data);
        op.immval = 0;
        data += 2;
        length += 2;
        break;    
    case OPR_IMM0:
        op.offset = 0;
        op.immval = 0;
        break;
    case OPR_IMM1:
        op.offset = 0;
        op.immval = 1;
        break;    
    case OPR_IMM8:
        op.offset = 0;
        op.immval = *reinterpret_cast<const SByte*>(data++);
        length++;
        break;    
    case OPR_IMM16:
        op.offset = 0;
        op.immval =  *reinterpret_cast<const SWord*>(data);
        data += 2;
        length += 2;
        break;
    case OPR_IMM32:
        op.offset = 0;
        op.immval =  *reinterpret_cast<const UDWord*>(data);
        data += 4;
        length += 4;
        break;
    default:
        throw CpuError("Invalid operand: "s + hexVal(op.type));
    }
}