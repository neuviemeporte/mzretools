#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>
#include <bitset>

#include "cpu.h"
#include "interrupt.h"
#include "util.h"
#include "error.h"

using namespace std;

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
    OP_SUB_Eb_Gb  = 0x28,
    OP_SUB_Ev_Gv  = 0x29,
    OP_SUB_Gb_Eb  = 0x2a,
    OP_SUB_Gv_Ev  = 0x2b,
    OP_SUB_AL_Ib  = 0x2c,
    OP_SUB_AX_Iv  = 0x2d,
    OP_PREFIX_CS  = 0x2e,
    OP_XOR_Eb_Gb  = 0x30,
    OP_XOR_Ev_Gv  = 0x31,
    OP_XOR_Gb_Eb  = 0x32,
    OP_XOR_Gv_Ev  = 0x33,
    OP_XOR_AL_Ib  = 0x34,
    OP_XOR_AX_Iv  = 0x35,
    OP_PREFIX_SS  = 0x36,
    OP_CMP_Eb_Gb  = 0x38,
    OP_CMP_Ev_Gv  = 0x39,
    OP_CMP_Gb_Eb  = 0x3a,
    OP_CMP_Gv_Ev  = 0x3b,
    OP_CMP_AL_Ib  = 0x3c,
    OP_CMP_AX_Iv  = 0x3d,
    OP_PREFIX_DS  = 0x3e,
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
    OP_RET_Iw     = 0xc2,
    OP_RET        = 0xc3,
    OP_LES_Gv_Mp  = 0xc4,
    OP_LDS_Gv_Mp  = 0xc5,
    OP_MOV_Eb_Ib  = 0xc6,
    OP_MOV_Ev_Iv  = 0xc7,
    OP_RETF_Iw    = 0xca,
    OP_RETF       = 0xcb,
    OP_INT_3      = 0xcc,
    OP_INT_Ib     = 0xcd,
    OP_INTO       = 0xce,
    OP_IRET       = 0xcf,
    OP_XLAT       = 0xd7,
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
    OP_REPNZ      = 0xf2, // REPNE
    OP_REPZ       = 0xf3, // REP, REPE
    OP_HLT        = 0xf4,
    OP_CMC        = 0xf5,
    OP_CLC        = 0xf8,
    OP_STC        = 0xf9,
    OP_CLI        = 0xfa,
    OP_STI        = 0xfb,
    OP_CLD        = 0xfc,
    OP_STD        = 0xfd,
};

void cpuMessage(const string &msg) {
    cout << msg << endl;
}

Cpu_8086::Cpu_8086(Byte *memBase, InterruptInterface *intif) : memBase_(memBase), code_(nullptr), int_(intif), 
    opcode_(OP_NOP), modrm_(0), 
    byteOperand1_(0), byteOperand2_(0), byteResult_(0),
    wordOperand1_(0), wordOperand2_(0), wordResult_(0),
    segmentReg_(REG_NONE), destReg_(REG_NONE),
    done_(false), step_(false) {
    resetRegs();
}

std::string Cpu_8086::info() const {
    ostringstream str;
    const Word flags = reg16(REG_FLAGS);
    str << std::hex 
        << "IP = " << hexVal(reg16(REG_IP)) << ", FLAGS = " << hexVal(flags) << " / " << binString(flags) << " / " << flagString(flags) << endl
        << "AX = " << hexVal(reg16(REG_AX)) << ", BX = " << hexVal(reg16(REG_BX)) << ", CX = " << hexVal(reg16(REG_CX)) << ", DX = " << hexVal(reg16(REG_DX)) << endl
        << "SI = " << hexVal(reg16(REG_SI)) << ", DI = " << hexVal(reg16(REG_DI)) << ", BP = " << hexVal(reg16(REG_BP)) << ", SP = " << hexVal(reg16(REG_SP)) << endl
        << "CS = " << hexVal(reg16(REG_CS)) << ", DS = " << hexVal(reg16(REG_DS)) << ", SS = " << hexVal(reg16(REG_SS)) << ", ES = " << hexVal(reg16(REG_ES));
    return str.str();
}

string Cpu_8086::flagString(const Word flags) const {
    std::ostringstream str; // // C1 Z1 S0 O0 A1 P1
    str << "C" << ((flags & FLAG_CARRY) != 0) << " Z" << ((flags & FLAG_ZERO) != 0) << " S" << ((flags & FLAG_SIGN  ) != 0)
       << " O" << ((flags & FLAG_OVER ) != 0) << " A" << ((flags & FLAG_AUXC) != 0) << " P" << ((flags & FLAG_PARITY) != 0)
       << " D" << ((flags & FLAG_DIR  ) != 0) << " I" << ((flags & FLAG_INT ) != 0) << " T" << ((flags & FLAG_TRAP  ) != 0);
    return str.str();
}


// parity flag values for all possible 8-bit results, used as a lookup table
// to avoid counting 1 bits after an arithmetic instruction 
// (8086 only checks the lower 8 bits, even for 16bit results)
static const Word parity_lookup[256] = {
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

// ModR/M byte semantics: it follows some instructions and has the following bits: MMRRRTTT
// MM: MOD field:
//    00: interpret TTT as Table1 to calculate address of memory operand
//    01: interpret TTT as Table2 with 8bit signed displacement to calculate address of memory operand
//    10: interpret TTT as Table2 with 16bit unsigned displacement to calculate address of memory operand
//    11: interpret TTT as REG to use register as operand
// RRR: REG field:
//    8bit instruction:  000: AL     001: CL     010: DL     011: BL     100: AH     101: CH     110: DH     111: BH
//    16bit instruction: 000: AX     001: CX     010: DX     011: BX     100: SP     101: BP     110: SI     111: DI
//    segment register:  000: ES     001: CS     010: SS     011: DS
// TTT: MEM field:
// TTT Table1 (when MOD = 00)
//    000: [BX+SI]      001: [BX+DI]     010: [BP+SI]           011: [BP+DI]
//    100: [SI]         101: [DI]        110: direct address    111: [BX] 
// TTT Table2 (when MOD = 01 or 10):
//    000: [BX+SI+Offset]     001: [BX+DI+Offset]     010: [BP+SI+Offset]     011: [BP+DI+Offset]
//    100: [SI+Offset]        101: [DI+Offset]        110: [BP+Offset]        111: [BX+Offset]


// possible values of the MOD field in the ModR/M byte
enum ModRMMod : Byte {
    MODRM_NODISP = 0, 
    MODRM_DISP8 = 1, 
    MODRM_DISP16 = 2, 
    MODRM_REG = 3,
};

// register ids of subsequent values of the REG field in the ModR/M byte
static const Register modrm_regs8[8] = {
    REG_AL, REG_CL, REG_DL, REG_BL, REG_AH, REG_CH, REG_DH, REG_BH    
};

static const Register modrm_regs16[8] = {
    REG_AX, REG_CX, REG_DX, REG_BX, REG_SP, REG_BP, REG_SI, REG_DI
};

static const Register modrm_segreg[4] = {
    REG_ES, REG_CS, REG_SS, REG_DS
};

// possible values of the MEM field in the ModR/M byte, both Table1 and Table2
enum ModRMAddress : Byte {
    MODRM_BX_SI = 0, MODRM_BX_SI_OFF = 0,
    MODRM_BX_DI = 1, MODRM_BX_DI_OFF = 1,
    MODRM_BP_SI = 2, MODRM_BP_SI_OFF = 2,
    MODRM_BP_DI = 3, MODRM_BP_DI_OFF = 3,
    MODRM_SI    = 4, MODRM_SI_OFF    = 4,
    MODRM_DI    = 5, MODRM_DI_OFF    = 5,
    MODRM_ADDR  = 6, MODRM_BP_OFF    = 6,
    MODRM_BX    = 7, MODRM_BX_OFF    = 7,
};

Register Cpu_8086::destinationRegister(const Byte opcode) const {
    return REG_NONE;
}

Register Cpu_8086::defaultSeg(const Register reg) const {
    switch (reg) {
    case REG_IP: return REG_CS;
    case REG_SP:
    case REG_BP: return REG_SS;
    case REG_DI: return REG_ES;
    default: return REG_DS;
    }
}

Offset Cpu_8086::modrmAddress(const Byte mod, const Byte rm) const {
    Register baseReg;
    // obtain byte/word offset value for the displacement addressing modes, if applicable
    Offset displacement, address;
    switch (mod) {
    // assuming cs:ip points at the opcode, so the offset will be 2 bytes past that,
    // after the opcode and the modrm byte itself
    case MODRM_DISP8:  displacement = ipByte(2); break; 
    case MODRM_DISP16: displacement = ipWord(2); break;
    }
    // calculate address of the word or byte from the register values and optionally the displacement
    switch (mod) {
    // no displacement mode
    case MODRM_NODISP: 
        switch (rm) {
        case MODRM_BX_SI: baseReg = REG_BX;   address = reg16(REG_BX) + reg16(REG_SI); break;
        case MODRM_BX_DI: baseReg = REG_BX;   address = reg16(REG_BX) + reg16(REG_DI); break;
        case MODRM_BP_SI: baseReg = REG_BP;   address = reg16(REG_BP) + reg16(REG_SI); break;
        case MODRM_BP_DI: baseReg = REG_BP;   address = reg16(REG_BP) + reg16(REG_DI); break;
        case MODRM_SI:    baseReg = REG_SI;   address = reg16(REG_SI); break;
        case MODRM_DI:    baseReg = REG_DI;   address = reg16(REG_DI); break;
        case MODRM_ADDR:  baseReg = REG_NONE; address = ipWord(2); break; // address is the word past the opcode and modrm byte itself
        case MODRM_BX:    baseReg = REG_BX;   address = reg16(REG_BX); break;
        }
        break;
    // 8- and 16-bit displacement modes
    case MODRM_DISP8: // fall through
    case MODRM_DISP16:
        switch (rm) {
        case MODRM_BX_SI_OFF: baseReg = REG_BX; address = reg16(REG_BX) + reg16(REG_SI) + displacement; break;
        case MODRM_BX_DI_OFF: baseReg = REG_BX; address = reg16(REG_BX) + reg16(REG_DI) + displacement; break;
        case MODRM_BP_SI_OFF: baseReg = REG_BP; address = reg16(REG_BP) + reg16(REG_SI) + displacement; break;
        case MODRM_BP_DI_OFF: baseReg = REG_BP; address = reg16(REG_BP) + reg16(REG_DI) + displacement; break;
        case MODRM_SI_OFF:    baseReg = REG_SI; address = reg16(REG_SI) + displacement; break;
        case MODRM_DI_OFF:    baseReg = REG_DI; address = reg16(REG_DI) + displacement; break;
        case MODRM_BP_OFF:    baseReg = REG_BP; address = reg16(REG_BP) + displacement; break;
        case MODRM_BX_OFF:    baseReg = REG_BX; address = reg16(REG_BX) + displacement; break;
        }
        break;
    }
    // the calculated address is relative to a segment that is implicitly associated with the base register
    // TODO: implement segment override prefix which changes the default segment
    Offset segAddress = SEG_TO_LINEAR(reg16(defaultSeg(baseReg)));
    return segAddress + address;
}

// obtain the byte value pointed to by the ModR/M byte of an instruction
Byte Cpu_8086::modrmByte(const Byte modrm) const {
    const Byte 
        mod = modrm_mod(modrm),
        rm  = modrm_rm(modrm);
    switch (mod) {
    // modrm byte indicates a memory operand, calculate its address and return it
    case MODRM_NODISP:
    case MODRM_DISP8: // fall through
    case MODRM_DISP16:
        return memBase_[modrmAddress(mod, rm)];
    // modrm byte indicates a register
    case MODRM_REG:
        return reg8(modrm_regs8[rm]);
    }
}

Word Cpu_8086::modrmWord(const Byte modrm) const {
    const Byte 
        mod = modrm_mod(modrm),
        rm  = modrm_rm(modrm);
    switch (mod) {
    // modrm byte indicates a memory operand, calculate its address and return it
    case MODRM_NODISP:
    case MODRM_DISP8: // fall through
    case MODRM_DISP16:
        return *reinterpret_cast<Word*>(memBase_ + modrmAddress(mod, rm));
    // modrm byte indicates a register
    case MODRM_REG:
        return reg16(modrm_regs16[rm]);
    }
}

// calculate length of instruction with modrm byte
Word Cpu_8086::modrmLength(const Byte modrm) const {
    const Word length = 2; // at least opcode + modrm byte
    switch (modrm_mod(modrm)) {
    case MODRM_NODISP:
        switch (modrm_rm(modrm)) {
        // opcode + modrm + direct address word
        case MODRM_ADDR: return length + 2;
        // opcode + modrm
        default: return length;
        }
    case MODRM_DISP8:
        // opcode + modrm + displacement byte
        return length + 1;
    case MODRM_DISP16:
        // opcode + modrm + displacement word
        return length + 2;
    case MODRM_REG:
        // opcode + modrm
        return length;   
    }
}

void Cpu_8086::init(const Address &codeAddr, const Address &stackAddr) {
    // initialize registers
    resetRegs();
    reg16(REG_CS) = codeAddr.segment;
    reg16(REG_IP) = codeAddr.offset;
    reg16(REG_SS) = stackAddr.segment;
    reg16(REG_SP) = stackAddr.offset;
    Address codeSeg(reg16(REG_CS), 0);
    const Offset codeLinearAddr = codeSeg.toLinear();
    const Offset pspLinearAddr = codeLinearAddr - PSP_SIZE;
    Address pspAddr(pspLinearAddr);
    assert(pspAddr.offset == 0);
    reg16(REG_DS) = reg16(REG_ES) = pspAddr.segment;

    // obtain pointer to beginning of executable code
    code_ = memBase_ + codeLinearAddr;
}

void Cpu_8086::step() {
    step_ = true;
    pipeline();
}

void Cpu_8086::run() {
    step_ = false;
    pipeline();
}

void Cpu_8086::resetRegs() {
    fill(begin(regs_.reg16), end(regs_.reg16), 0);
    setFlag(FLAG_INT, true);
    // these are supposed to be always 1 on 8086
    setFlag(FLAG_B12, true); 
    setFlag(FLAG_B13, true);
    setFlag(FLAG_B14, true); 
    setFlag(FLAG_B15, true);
}

// main loop for evaluating and executing instructions
void Cpu_8086::pipeline() {
    done_ = false;
    while (!done_) {
        fetch();
        evaluate();
        commit();
        advance();
        if (step_) break;
    }
}

void Cpu_8086::fetch() {
    // fetch opcode
    opcode_ = ipByte();
    // fetch value of first operand (optional)
    switch (opcode_) {
    case OP_MOV_Gv_Ev:
        modrm_ = ipByte(1);
        destReg_ = modrm_regs16[modrm_reg(modrm_)];
        wordOperand1_ = modrmWord(modrm_);
        return;
    case OP_ADD_AL_Ib: 
    case OP_SUB_AL_Ib:
    case OP_CMP_AL_Ib:
        byteOperand1_ = reg8(REG_AL); 
        break;
    case OP_JNB_Jb:
    case OP_MOV_AL_Ib: 
    case OP_MOV_AH_Ib:
    case OP_INT_Ib:
        byteOperand1_ = ipByte(1); 
        return;
    case OP_MOV_DI_Iv:
        wordOperand1_ = ipWord(1); 
        return;
    default: unknown("fetch#1");
    }
    // fetch value of second operand (optional)
    switch (opcode_) {
    case OP_ADD_AL_Ib: 
    case OP_SUB_AL_Ib:
    case OP_CMP_AL_Ib:
        byteOperand2_ = ipByte(1); break;
    default: unknown("fetch#2");
    }    
}

static const Byte NIBBLE_CARRY = 0x10;
static const Byte BYTE_SIGN = 0x80;
static const Word WORD_SIGN = 0x8000;

void Cpu_8086::evaluate() {
    // evaluate result, update carry and overflow
    switch (opcode_) {
    case OP_ADD_AL_Ib: 
        byteResult_ = byteOperand1_ + byteOperand2_;
        setFlag(FLAG_CARRY, byteResult_ < byteOperand1_);
        setFlag(FLAG_OVER, ((byteOperand1_ ^ byteOperand2_ ^ BYTE_SIGN) & (byteResult_ ^ byteOperand2_)) & BYTE_SIGN);
        break;
    case OP_SUB_AL_Ib:
    case OP_CMP_AL_Ib:
        byteResult_ = byteOperand1_ - byteOperand2_;
        setFlag(FLAG_CARRY, byteOperand1_ < byteOperand2_);
        setFlag(FLAG_OVER, ((byteOperand1_ ^ byteOperand2_) & (byteOperand1_ ^ byteResult_)) & BYTE_SIGN);
        break;
    case OP_JNB_Jb:
        byteResult_ = getFlag(FLAG_CARRY) ? 0 : byteOperand1_;
        return;
    case OP_MOV_AL_Ib:
    case OP_MOV_AH_Ib:
        byteResult_ = byteOperand1_; // this lets mov share the switch with add, sub etc. below
        return; 
    case OP_MOV_Gv_Ev:
    case OP_MOV_DI_Iv:
        wordResult_ = wordOperand1_; // likewise
        return; 
    case OP_INT_Ib:
        return;
    default: unknown("evaluate#1");
    }
    // update remaining arithmetic flags
    switch (opcode_)
    {
    case OP_ADD_AL_Ib: 
    case OP_SUB_AL_Ib:
    case OP_CMP_AL_Ib:
        setFlag(FLAG_ZERO, byteResult_ == 0);
        setFlag(FLAG_SIGN, byteResult_ & BYTE_SIGN);
        setFlag(FLAG_AUXC, ((byteOperand1_ ^ byteOperand2_) ^ byteResult_) & NIBBLE_CARRY);
        setFlag(FLAG_PARITY, parity_lookup[byteResult_]);
        break;
    default: unknown("evaluate#2");        
    }
}

void Cpu_8086::commit() {
    // write evaluation result (if any) to a register, memory or wherever it belongs,
    // or trigger an external action (interrupt, IO port r/w)
    switch (opcode_) {
    case OP_CMP_AL_Ib:
    case OP_JNB_Jb:
        return;
    case OP_MOV_Gv_Ev:
        reg16(destReg_) = wordResult_;
        break;
    case OP_ADD_AL_Ib: 
    case OP_SUB_AL_Ib:        
    case OP_MOV_AL_Ib:
        reg8(REG_AL) = byteResult_; 
        break;
    case OP_MOV_AH_Ib: 
        reg8(REG_AH) = byteResult_; 
        break;
    case OP_MOV_DI_Iv:
        reg16(REG_DI) = wordResult_; 
        break;
    case OP_INT_Ib:
        switch (int_->interrupt(*this, byteOperand1_)) {
        case INT_EXIT:
            done_ = true;
            break;
        case INT_TERMINATE:
            throw CpuError("Program terminated at "s + csip().toString());
        }
        break;
    default: unknown("commit");
    }
}

void Cpu_8086::advance() {
    Word amount;
    switch (opcode_) {
    case OP_MOV_Gv_Ev: amount = modrmLength(modrm_); break;
    case OP_JNB_Jb:    amount = 2 + byteResult_; break;
    case OP_ADD_AL_Ib: 
    case OP_SUB_AL_Ib:
    case OP_CMP_AL_Ib:
    case OP_MOV_AL_Ib:
    case OP_MOV_AH_Ib:
    case OP_INT_Ib:    amount = 2; break;
    case OP_MOV_DI_Iv: amount = 3; break;
    default: unknown("advance");
    }
    ipAdvance(amount);
}

void Cpu_8086::unknown(const string &stage) {
    throw CpuError("Unknown opcode at "s + stage + " stage, address "s + csip().toString() + ": " + hexVal(opcode_));
}

