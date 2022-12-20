#ifndef OPCODES_H
#define OPCODES_H

#include "dos/types.h"

// Legend for abbreviations used in opcode names:
// 'v' and not 'w' is used for word-sized operands because the opcode map I used to do this employed this letter, supposedly 'w' and 'v' have some distinction for later (than 8086) x86 instruction opcodes
// Gb/Gv: byte or word-sized operand, can only refer to a general purpose register, based on the REG value in the modrm byte following the opcode
// Eb/Ev: byte or word-sized operand, can refer to either memory, or a register, based on the MOD and MEM values in the modrm byte following the opcode
// M/Mp: word, or dword-sized operand, can only refer to memory, based on the MEM value in the modrm byte following the opcode
// Ap: direct 32-bit far pointer, opcode has no modrm-byte, the pointer data follows the opcode directly
// Ib/Iv: byte, or word-sized immediate value
// Jb/Jv: byte, or word-sized jump offset
// Ob/Ov: byte, or word-sized offset directly following the opcode (no modrm byte)
// Sw: segment register operand, based on the REG value in the modrm byte following the opcode
enum Opcode : Byte {
    OP_ADD_Eb_Gb  = 0x00,
    OP_ADD_Ev_Gv  = 0x01,
    OP_ADD_Gb_Eb  = 0x02,
    OP_ADD_Gv_Ev  = 0x03,
    OP_ADD_AL_Ib  = 0x04,
    OP_ADD_AX_Iv  = 0x05,
    OP_PUSH_ES    = 0x06,
    OP_POP_ES     = 0x07,
    OP_OR_Eb_Gb   = 0x08,
    OP_OR_Ev_Gv   = 0x09,
    OP_OR_Gb_Eb   = 0x0a,
    OP_OR_Gv_Ev   = 0x0b,
    OP_OR_AL_Ib   = 0x0c,
    OP_OR_AX_Iv   = 0x0d,
    OP_PUSH_CS    = 0x0e,
    OP_INVALID    = 0x0f,

    OP_ADC_Eb_Gb  = 0x10,
    OP_ADC_Ev_Gv  = 0x11,
    OP_ADC_Gb_Eb  = 0x12,
    OP_ADC_Gv_Ev  = 0x13,
    OP_ADC_AL_Ib  = 0x14,
    OP_ADC_AX_Iv  = 0x15,
    OP_PUSH_SS    = 0x16,
    OP_POP_SS     = 0x17,
    OP_SBB_Eb_Gb  = 0x18,
    OP_SBB_Ev_Gv  = 0x19,
    OP_SBB_Gb_Eb  = 0x1a,
    OP_SBB_Gv_Ev  = 0x1b,
    OP_SBB_AL_Ib  = 0x1c,
    OP_SBB_AX_Iv  = 0x1d,
    OP_PUSH_DS    = 0x1e,
    OP_POP_DS     = 0x1f,

    OP_AND_Eb_Gb  = 0x20,
    OP_AND_Ev_Gv  = 0x21,
    OP_AND_Gb_Eb  = 0x22,
    OP_AND_Gv_Ev  = 0x23,
    OP_AND_AL_Ib  = 0x24,
    OP_AND_AX_Iv  = 0x25,
    OP_PREFIX_ES  = 0x26,
    OP_DAA        = 0x27,
    OP_SUB_Eb_Gb  = 0x28,
    OP_SUB_Ev_Gv  = 0x29,
    OP_SUB_Gb_Eb  = 0x2a,
    OP_SUB_Gv_Ev  = 0x2b,
    OP_SUB_AL_Ib  = 0x2c,
    OP_SUB_AX_Iv  = 0x2d,
    OP_PREFIX_CS  = 0x2e,
    OP_DAS        = 0x2f,

    OP_XOR_Eb_Gb  = 0x30,
    OP_XOR_Ev_Gv  = 0x31,
    OP_XOR_Gb_Eb  = 0x32,
    OP_XOR_Gv_Ev  = 0x33,
    OP_XOR_AL_Ib  = 0x34,
    OP_XOR_AX_Iv  = 0x35,
    OP_PREFIX_SS  = 0x36,
    OP_AAA        = 0x37,
    OP_CMP_Eb_Gb  = 0x38,
    OP_CMP_Ev_Gv  = 0x39,
    OP_CMP_Gb_Eb  = 0x3a,
    OP_CMP_Gv_Ev  = 0x3b,
    OP_CMP_AL_Ib  = 0x3c,
    OP_CMP_AX_Iv  = 0x3d,
    OP_PREFIX_DS  = 0x3e,
    OP_AAS        = 0x3f,

    OP_INC_AX     = 0x40,
    OP_INC_CX     = 0x41,
    OP_INC_DX     = 0x42,
    OP_INC_BX     = 0x43,
    OP_INC_SP     = 0x44,
    OP_INC_BP     = 0x45,
    OP_INC_SI     = 0x46,
    OP_INC_DI     = 0x47,
    OP_DEC_AX     = 0x48,
    OP_DEC_CX     = 0x49,
    OP_DEC_DX     = 0x4a,
    OP_DEC_BX     = 0x4b,
    OP_DEC_SP     = 0x4c,
    OP_DEC_BP     = 0x4d,
    OP_DEC_SI     = 0x4e,
    OP_DEC_DI     = 0x4f,

    OP_PUSH_AX    = 0x50,
    OP_PUSH_CX    = 0x51,
    OP_PUSH_DX    = 0x52,
    OP_PUSH_BX    = 0x53,
    OP_PUSH_SP    = 0x54,
    OP_PUSH_BP    = 0x55,
    OP_PUSH_SI    = 0x56,
    OP_PUSH_DI    = 0x57,
    OP_POP_AX     = 0x58,
    OP_POP_CX     = 0x59,
    OP_POP_DX     = 0x5a,
    OP_POP_BX     = 0x5b,
    OP_POP_SP     = 0x5c,
    OP_POP_BP     = 0x5d,
    OP_POP_SI     = 0x5e,
    OP_POP_DI     = 0x5f,

    // 0x60
    // 0x61
    // 0x62
    // 0x63
    // 0x64
    // 0x65
    // 0x66
    // 0x67
    // 0x68
    // 0x69
    // 0x6a
    // 0x6b
    // 0x6c
    // 0x6d
    // 0x6e
    // 0x6f
    
    OP_JO_Jb      = 0x70,
    OP_JNO_Jb     = 0x71,
    OP_JB_Jb      = 0x72,
    OP_JNB_Jb     = 0x73,
    OP_JZ_Jb      = 0x74,
    OP_JNZ_Jb     = 0x75,
    OP_JBE_Jb     = 0x76,
    OP_JA_Jb      = 0x77,
    OP_JS_Jb      = 0x78,
    OP_JNS_Jb     = 0x79,
    OP_JPE_Jb     = 0x7a,
    OP_JPO_Jb     = 0x7b,
    OP_JL_Jb      = 0x7c,
    OP_JGE_Jb     = 0x7d,
    OP_JLE_Jb     = 0x7e,
    OP_JG_Jb      = 0x7f,

    OP_GRP1_Eb_Ib = 0x80,
    OP_GRP1_Ev_Iv = 0x81,
    OP_GRP1_Eb_Ib2= 0x82, // this is a synonym for 0x80?
    OP_GRP1_Ev_Ib = 0x83,
    OP_TEST_Gb_Eb = 0x84,
    OP_TEST_Gv_Ev = 0x85,
    OP_XCHG_Gb_Eb = 0x86,
    OP_XCHG_Gv_Ev = 0x87,
    OP_MOV_Eb_Gb  = 0x88,
    OP_MOV_Ev_Gv  = 0x89,
    OP_MOV_Gb_Eb  = 0x8a,
    OP_MOV_Gv_Ev  = 0x8b,
    OP_MOV_Ew_Sw  = 0x8c,
    OP_LEA_Gv_M   = 0x8d,
    OP_MOV_Sw_Ew  = 0x8e,
    OP_POP_Ev     = 0x8f,

    OP_NOP        = 0x90,
    OP_XCHG_CX_AX = 0x91,
    OP_XCHG_DX_AX = 0x92,
    OP_XCHG_BX_AX = 0x93,
    OP_XCHG_SP_AX = 0x94,
    OP_XCHG_BP_AX = 0x95,
    OP_XCHG_SI_AX = 0x96,
    OP_XCHG_DI_AX = 0x97,
    OP_CBW        = 0x98,
    OP_CWD        = 0x99,
    OP_CALL_Ap    = 0x9a,
    OP_WAIT       = 0x9b,
    OP_PUSHF      = 0x9c,
    OP_POPF       = 0x9d,
    OP_SAHF       = 0x9e,
    OP_LAHF       = 0x9f,

    OP_MOV_AL_Ob  = 0xa0,
    OP_MOV_AX_Ov  = 0xa1,
    OP_MOV_Ob_AL  = 0xa2,
    OP_MOV_Ov_AX  = 0xa3,
    OP_MOVSB      = 0xa4,
    OP_MOVSW      = 0xa5,
    OP_CMPSB      = 0xa6,
    OP_CMPSW      = 0xa7,
    OP_TEST_AL_Ib = 0xa8,
    OP_TEST_AX_Iv = 0xa9,
    OP_STOSB      = 0xaa,
    OP_STOSW      = 0xab,
    OP_LODSB      = 0xac,
    OP_LODSW      = 0xad,
    OP_SCASB      = 0xae,
    OP_SCASW      = 0xaf,

    OP_MOV_AL_Ib  = 0xb0,
    OP_MOV_CL_Ib  = 0xb1,
    OP_MOV_DL_Ib  = 0xb2,
    OP_MOV_BL_Ib  = 0xb3,
    OP_MOV_AH_Ib  = 0xb4,
    OP_MOV_CH_Ib  = 0xb5,
    OP_MOV_DH_Ib  = 0xb6,
    OP_MOV_BH_Ib  = 0xb7,
    OP_MOV_AX_Iv  = 0xb8,
    OP_MOV_CX_Iv  = 0xb9,
    OP_MOV_DX_Iv  = 0xba,
    OP_MOV_BX_Iv  = 0xbb,
    OP_MOV_SP_Iv  = 0xbc,
    OP_MOV_BP_Iv  = 0xbd,
    OP_MOV_SI_Iv  = 0xbe,
    OP_MOV_DI_Iv  = 0xbf,

    // 0xc0
    // 0xc1
    OP_RET_Iw     = 0xc2,
    OP_RET        = 0xc3,
    OP_LES_Gv_Mp  = 0xc4,
    OP_LDS_Gv_Mp  = 0xc5,
    OP_MOV_Eb_Ib  = 0xc6,
    OP_MOV_Ev_Iv  = 0xc7,
    // 0xc8
    // 0xc9
    OP_RETF_Iw    = 0xca,
    OP_RETF       = 0xcb,
    OP_INT_3      = 0xcc,
    OP_INT_Ib     = 0xcd,
    OP_INTO       = 0xce,
    OP_IRET       = 0xcf,

    OP_GRP2_Eb_1  = 0xd0,
    OP_GRP2_Ev_1  = 0xd1,
    OP_GRP2_Eb_CL = 0xd2,
    OP_GRP2_Ev_CL = 0xd3,
    OP_AAM_I0     = 0xd4,
    OP_AAD_I0     = 0xd5,
    // 0xd6
    OP_XLAT       = 0xd7,
    // 0xd8
    // 0xd9
    // 0xda
    // 0xdb
    // 0xdc
    // 0xdd
    // 0xde
    // 0xdf

    OP_LOOPNZ_Jb  = 0xe0,
    OP_LOOPZ_Jb   = 0xe1,
    OP_LOOP_Jb    = 0xe2,
    OP_JCXZ_Jb    = 0xe3,
    OP_IN_AL_Ib   = 0xe4,
    OP_IN_AX_Ib   = 0xe5,
    OP_OUT_Ib_AL  = 0xe6,
    OP_OUT_Ib_AX  = 0xe7,
    OP_CALL_Jv    = 0xe8,
    OP_JMP_Jv     = 0xe9,
    OP_JMP_Ap     = 0xea,
    OP_JMP_Jb     = 0xeb,
    OP_IN_AL_DX   = 0xec,
    OP_IN_AX_DX   = 0xed,
    OP_OUT_DX_AL  = 0xee,
    OP_OUT_DX_AX  = 0xef,
    
    OP_LOCK       = 0xf0,
    // 0xf1
    OP_REPNZ      = 0xf2, // REPNE
    OP_REPZ       = 0xf3, // REP, REPE
    OP_HLT        = 0xf4,
    OP_CMC        = 0xf5,
    OP_GRP3a_Eb   = 0xf6,
    OP_GRP3b_Ev   = 0xf7,
    OP_CLC        = 0xf8,
    OP_STC        = 0xf9,
    OP_CLI        = 0xfa,
    OP_STI        = 0xfb,
    OP_CLD        = 0xfc,
    OP_STD        = 0xfd,
    OP_GRP4_Eb    = 0xfe,
    OP_GRP5_Ev    = 0xff,
};



std::string opcodeName(const Byte opcode);
std::string opcodeString(const Byte opcode);
bool opcodeIsModrm(const Byte opcode);
bool opcodeIsGroup(const Byte opcode);
bool opcodeIsSegmentPrefix(const Byte opcode);
size_t opcodeInstructionLength(const Byte opcode);

#endif // OPCODES_H