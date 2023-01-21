#include "dos/opcodes.h"
#include "dos/types.h"
#include "dos/util.h"

#include <string>

using namespace std;

static const char* OPCODE_NAMES[256] = {
    "ADD_Eb_Gb",
    "ADD_Ev_Gv",
    "ADD_Gb_Eb",
    "ADD_Gv_Ev",
    "ADD_AL_Ib",
    "ADD_AX_Iv",
    "PUSH_ES",
    "POP_ES",
    "OR_Eb_Gb",
    "OR_Ev_Gv",
    "OR_Gb_Eb",
    "OR_Gv_Ev",
    "OR_AL_Ib",
    "OR_AX_Iv",
    "PUSH_CS",
    "XXX_0x0f",
    "ADC_Eb_Gb",
    "ADC_Ev_Gv",
    "ADC_Gb_Eb",
    "ADC_Gv_Ev",
    "ADC_AL_Ib",
    "ADC_AX_Iv",
    "PUSH_SS",
    "POP_SS",
    "SBB_Eb_Gb",
    "SBB_Ev_Gv",
    "SBB_Gb_Eb",
    "SBB_Gv_Ev",
    "SBB_AL_Ib",
    "SBB_AX_Iv",
    "PUSH_DS",
    "POP_DS",
    "AND_Eb_Gb",
    "AND_Ev_Gv",
    "AND_Gb_Eb",
    "AND_Gv_Ev",
    "AND_AL_Ib",
    "AND_AX_Iv",
    "PREFIX_ES",
    "DAA",
    "SUB_Eb_Gb",
    "SUB_Ev_Gv",
    "SUB_Gb_Eb",
    "SUB_Gv_Ev",
    "SUB_AL_Ib",
    "SUB_AX_Iv",
    "PREFIX_CS",
    "DAS",
    "XOR_Eb_Gb",
    "XOR_Ev_Gv",
    "XOR_Gb_Eb",
    "XOR_Gv_Ev",
    "XOR_AL_Ib",
    "XOR_AX_Iv",
    "PREFIX_SS",
    "AAA",
    "CMP_Eb_Gb",
    "CMP_Ev_Gv",
    "CMP_Gb_Eb",
    "CMP_Gv_Ev",
    "CMP_AL_Ib",
    "CMP_AX_Iv",
    "PREFIX_DS",
    "AAS",
    "INC_AX",
    "INC_CX",
    "INC_DX",
    "INC_BX",
    "INC_SP",
    "INC_BP",
    "INC_SI",
    "INC_DI",
    "DEC_AX",
    "DEC_CX",
    "DEC_DX",
    "DEC_BX",
    "DEC_SP",
    "DEC_BP",
    "DEC_SI",
    "DEC_DI",
    "PUSH_AX",
    "PUSH_CX",
    "PUSH_DX",
    "PUSH_BX",
    "PUSH_SP",
    "PUSH_BP",
    "PUSH_SI",
    "PUSH_DI",
    "POP_AX",
    "POP_CX",
    "POP_DX",
    "POP_BX",
    "POP_SP",
    "POP_BP",
    "POP_SI",
    "POP_DI",
    "XXX_0x60",
    "XXX_0x61",
    "XXX_0x62",
    "XXX_0x63",
    "XXX_0x64",
    "XXX_0x65",
    "XXX_0x66",
    "XXX_0x67",
    "XXX_0x68",
    "XXX_0x69",
    "XXX_0x6a",
    "XXX_0x6b",
    "XXX_0x6c",
    "XXX_0x6d",
    "XXX_0x6e",
    "XXX_0x6f",
    "JO_Jb",
    "JNO_Jb",
    "JB_Jb",
    "JNB_Jb",
    "JZ_Jb",
    "JNZ_Jb",
    "JBE_Jb",
    "JA_Jb",
    "JS_Jb",
    "JNS_Jb",
    "JPE_Jb",
    "JPO_Jb",
    "JL_Jb",
    "JGE_Jb",
    "JLE_Jb",
    "JG_Jb",
    "GRP1_Eb_Ib",
    "GRP1_Ev_Iv",
    "GRP1_Eb_Ib2",
    "GRP1_Ev_Ib",
    "TEST_Gb_Eb",
    "TEST_Gv_Ev",
    "XCHG_Gb_Eb",
    "XCHG_Gv_Ev",
    "MOV_Eb_Gb",
    "MOV_Ev_Gv",
    "MOV_Gb_Eb",
    "MOV_Gv_Ev",
    "MOV_Ew_Sw",
    "LEA_Gv_M",
    "MOV_Sw_Ew",
    "POP_Ev",
    "NOP",
    "XCHG_CX_AX",
    "XCHG_DX_AX",
    "XCHG_BX_AX",
    "XCHG_SP_AX",
    "XCHG_BP_AX",
    "XCHG_SI_AX",
    "XCHG_DI_AX",
    "CBW",
    "CWD",
    "CALL_Ap",
    "WAIT",
    "PUSHF",
    "POPF",
    "SAHF",
    "LAHF",
    "MOV_AL_Ob",
    "MOV_AX_Ov",
    "MOV_Ob_AL",
    "MOV_Ov_AX",
    "MOVSB",
    "MOVSW",
    "CMPSB",
    "CMPSW",
    "TEST_AL_Ib",
    "TEST_AX_Iv",
    "STOSB",
    "STOSW",
    "LODSB",
    "LODSW",
    "SCASB",
    "SCASW",
    "MOV_AL_Ib",
    "MOV_CL_Ib",
    "MOV_DL_Ib",
    "MOV_BL_Ib",
    "MOV_AH_Ib",
    "MOV_CH_Ib",
    "MOV_DH_Ib",
    "MOV_BH_Ib",
    "MOV_AX_Iv",
    "MOV_CX_Iv",
    "MOV_DX_Iv",
    "MOV_BX_Iv",
    "MOV_SP_Iv",
    "MOV_BP_Iv",
    "MOV_SI_Iv",
    "MOV_DI_Iv",
    "XXX_0xc0",
    "XXX_0xc1",
    "RET_Iw",
    "RET",
    "LES_Gv_Mp",
    "LDS_Gv_Mp",
    "MOV_Eb_Ib",
    "MOV_Ev_Iv",
    "XXX_0xc8",
    "XXX_0xc9",
    "RETF_Iw",
    "RETF",
    "INT_3",
    "INT_Ib",
    "INTO",
    "IRET",
    "GRP2_Eb_1",
    "GRP2_Ev_1",
    "GRP2_Eb_CL",
    "GRP2_Ev_CL",
    "AAM_I0",
    "AAD_I0",
    "XXX_0xd6",
    "XLAT",
    "XXX_0xd8",
    "XXX_0xd9",
    "XXX_0xda",
    "XXX_0xdb",
    "XXX_0xdc",
    "XXX_0xdd",
    "XXX_0xde",
    "XXX_0xdf",
    "LOOPNZ_Jb",
    "LOOPZ_Jb",
    "LOOP_Jb",
    "JCXZ_Jb",
    "IN_AL_Ib",
    "IN_AX_Ib",
    "OUT_Ib_AL",
    "OUT_Ib_AX",
    "CALL_Jv",
    "JMP_Jv",
    "JMP_Ap",
    "JMP_Jb",
    "IN_AL_DX",
    "IN_AX_DX",
    "OUT_DX_AL",
    "OUT_DX_AX",
    "LOCK",
    "XXX_0xf1",
    "REPNZ",
    "REPZ",
    "HLT",
    "CMC",
    "GRP3a_Eb",
    "GRP3b_Ev",
    "CLC",
    "STC",
    "CLI",
    "STI",
    "CLD",
    "STD",
    "GRP4_Eb",
    "GRP5_Ev",
};

std::string opcodeName(const Byte opcode) {
    return string(OPCODE_NAMES[opcode]) + "/" + hexVal(opcode);
}

std::string opcodeString(const Byte opcode) {
    return hexVal(opcode) + " (" + opcodeName(opcode) + ")";
}

static const int OPCODE_MODRM[256] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, // 0
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, // 1
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, // 2
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 7
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, // C
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // E
    0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, // F
};

bool opcodeIsModrm(const Byte opcode) {
    return OPCODE_MODRM[opcode] == 1;
}

static const int OPCODE_GROUP[256] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 7
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // C
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // E
    0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, // F
};

bool opcodeIsGroup(const Byte opcode) {
    return OPCODE_GROUP[opcode] == 1;
}

static const int OPCODE_PREFIX[256] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 1
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, // 2
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 7
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F
};

bool opcodeIsSegmentPrefix(const Byte opcode) {
    return OPCODE_PREFIX[opcode] == 1;
}

bool opcodeIsConditionalJump(const Byte opcode) {
    return opcode >= OP_JO_Jb && opcode <= OP_JG_Jb;
}

// total length of instruction per opcode, not including the length of any displacements 
// that are optionally encoded in the instruction bytes of ModR/M opcodes
static const size_t OPCODE_INSTRLEN[256] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F    
    2, 2, 2, 2, 2, 3, 1, 1, 2, 2, 2, 2, 2, 3, 1, 0, // 0
    2, 2, 2, 2, 2, 3, 1, 1, 2, 2, 2, 2, 2, 3, 1, 1, // 1
    2, 2, 2, 2, 2, 3, 0, 0, 2, 2, 2, 2, 2, 3, 0, 0, // 2
    2, 2, 2, 2, 2, 3, 0, 0, 2, 2, 2, 2, 2, 3, 0, 0, // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 6
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // 7
    3, 4, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // 8
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 5, 1, 1, 1, 1, 1, // 9
    3, 3, 3, 3, 1, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, // A
    2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, // B
    0, 0, 3, 1, 4, 4, 3, 4, 0, 0, 3, 1, 1, 2, 1, 1, // C
    2, 2, 2, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, // D
    2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 5, 2, 1, 1, 1, 1, // E
    1, 0, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2, // F
};

size_t opcodeInstructionLength(const Byte opcode) {
    return OPCODE_INSTRLEN[opcode];
}