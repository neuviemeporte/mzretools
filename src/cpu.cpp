#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>
#include <bitset>

#include "dos/cpu.h"
#include "dos/opcodes.h"
#include "dos/interrupt.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

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
// TTT can also be another reg if MOD = 11, in which case its contents are interpreded same as RRR

// possible values of the MOD field in the ModR/M byte
enum ModRMMod : Byte {
    MODRM_NODISP = 0, 
    MODRM_DISP8 = 1, 
    MODRM_DISP16 = 2, 
    MODRM_REG = 3,
    MODRM_NOMOD
};

// possible values of the MEM field in the ModR/M byte, both Table1 and Table2
enum ModRMMem : Byte {
    MODRM_BX_SI = 0, MODRM_BX_SI_OFF = 0,
    MODRM_BX_DI = 1, MODRM_BX_DI_OFF = 1,
    MODRM_BP_SI = 2, MODRM_BP_SI_OFF = 2,
    MODRM_BP_DI = 3, MODRM_BP_DI_OFF = 3,
    MODRM_SI    = 4, MODRM_SI_OFF    = 4,
    MODRM_DI    = 5, MODRM_DI_OFF    = 5,
    MODRM_ADDR  = 6, MODRM_BP_OFF    = 6,
    MODRM_BX    = 7, MODRM_BX_OFF    = 7,
    MODRM_NOMEM
};

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

static void cpuMessage(const string &msg) {
    cout << msg << endl;
}

Cpu_8086::Cpu_8086(Memory *memory, InterruptInterface *inthandler) : 
    mem_(memory), int_(inthandler), 
    memBase_(nullptr), code_(nullptr),
    opcode_(OP_NOP), modrm_mod_(MODRM_NOMOD), modrm_reg_(0), modrm_mem_(MODRM_NOMEM),
    byteOperand1_(0), byteOperand2_(0), byteResult_(0),
    wordOperand1_(0), wordOperand2_(0), wordResult_(0),
    done_(false), step_(false) {
    memBase_ = mem_->base();
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

Register Cpu_8086::defaultSeg(const Register reg) const {
    switch (reg) {
    case REG_IP: return REG_CS;
    case REG_SP:
    case REG_BP: return REG_SS;
    case REG_DI: return REG_ES;
    default: return REG_DS;
    }
}

// load the value of the ModR/M byte following the instruction and split it into its mod, reg and r/m fields
void Cpu_8086::modrmParse() {
    const Byte modrm = ipByte(1); // assuming ip is at opcode, modrm at +1
    modrm_mod_ = modrm_mod(modrm);
    modrm_reg_ = modrm_reg(modrm);
    modrm_mem_ = modrm_mem(modrm);
}

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

// convert a ModR/M register value (from either the REG or the MEM field of the ModR/M byte) to our internal register identifier
Register Cpu_8086::modrmRegister(const RegType type, const Byte value) const {
    switch (type) {
    case REG_GP8:  return modrm_regs8[value];
    case REG_GP16: return modrm_regs16[value];
    case REG_SEG:  return modrm_segreg[value];
    default:
        throw CpuError("Invalid register type for ModR/M conversion: "s + to_string(type));
    }
}

// return id of register represented by the REG part of the ModR/M byte
inline Register Cpu_8086::modrmRegRegister(const RegType regType) const {
    return modrmRegister(regType, modrm_reg_);
}
// if the MEM part of the ModR/M byte actually represents a register (because MOD is 0b11), return its id, or invalid id otherwise
inline Register Cpu_8086::modrmMemRegister(const RegType regType) const {
    return (modrm_mod_ == MODRM_REG) ? modrmRegister(regType, modrm_mem_) : REG_NONE;
}

// calculate memory address to be used for the MEM operand of a ModR/M instruction, 
// the value of the MEM bits together with the MOD bits indicates the formula to use when calculating the address
Offset Cpu_8086::modrmMemAddress() const {
    Register baseReg;
    // obtain byte/word offset value for the displacement addressing modes, if applicable
    Offset displacement, address;
    switch (modrm_mod_) {
    // assuming cs:ip points at the opcode, so the offset will be 2 bytes past that,
    // after the opcode and the modrm byte itself
    case MODRM_DISP8:  displacement = ipByte(2); break; 
    case MODRM_DISP16: displacement = ipWord(2); break;
    }
    // calculate address of the word or byte from the register values and optionally the displacement
    switch (modrm_mod_) {
    // no displacement mode
    case MODRM_NODISP: 
        switch (modrm_mem_) {
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
        switch (modrm_mem_) {
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
    default:
        // can't calculate memory address for a register (MODRM_REG)
        throw CpuError("Invalid ModR/M for address calculation: "s + hexVal(modrm_mod_));
    }
    // the calculated address is relative to a segment that is implicitly associated with the base register
    // TODO: implement segment override prefix which changes the default segment
    Offset segAddress = SEG_TO_LINEAR(reg16(defaultSeg(baseReg)));
    return segAddress + address;
}

// calculate length of instruction with modrm byte
Word Cpu_8086::modrmInstructionLength() const {
    const Word length = 2; // at least opcode + modrm byte
    switch (modrm_mod_) {
    case MODRM_NODISP:
        switch (modrm_mem_) {
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
    throw CpuError("Invalid ModR/M MOD value while calculating instruction length: "s + hexVal(modrm_mod_));
}

void Cpu_8086::init(const Address &codeAddr, const Address &stackAddr) {
    // initialize registers
    resetRegs();
    reg16(REG_CS) = codeAddr.segment;
    reg16(REG_IP) = codeAddr.offset;
    reg16(REG_SS) = stackAddr.segment;
    reg16(REG_SP) = stackAddr.offset;
    const Offset codeLinearAddr = SEG_TO_LINEAR(reg16(REG_CS));
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
        opcode_ = ipByte();
        dispatch();
        if (step_) break;
    }
}

void Cpu_8086::dispatch() {
    switch (opcode_) {
    case OP_ADD_Eb_Gb :
    case OP_ADD_Ev_Gv :
    case OP_ADD_Gb_Eb :
    case OP_ADD_Gv_Ev :
    case OP_ADD_AL_Ib :
    case OP_ADD_AX_Iv :
    case OP_PUSH_ES   :
    case OP_POP_ES    :
    case OP_OR_Eb_Gb  :
    case OP_OR_Ev_Gv  :
    case OP_OR_Gb_Eb  :
    case OP_OR_Gv_Ev  :
    case OP_OR_AL_Ib  :
    case OP_OR_AX_Iv  :
    case OP_PUSH_CS   :
    case OP_ADC_Eb_Gb :
    case OP_ADC_Ev_Gv :
    case OP_ADC_Gb_Eb :
    case OP_ADC_Gv_Ev :
    case OP_ADC_AL_Ib :
    case OP_ADC_AX_Iv :
    case OP_PUSH_SS   :
    case OP_POP_SS    :
    case OP_SBB_Eb_Gb :
    case OP_SBB_Ev_Gv :
    case OP_SBB_Gb_Eb :
    case OP_SBB_Gv_Ev :
    case OP_SBB_AL_Ib :
    case OP_SBB_AX_Iv :
    case OP_PUSH_DS   :
    case OP_POP_DS    :
    case OP_AND_Eb_Gb :
    case OP_AND_Ev_Gv :
    case OP_AND_Gb_Eb :
    case OP_AND_Gv_Ev :
    case OP_AND_AL_Ib :
    case OP_AND_AX_Iv :
    case OP_PREFIX_ES :
    case OP_SUB_Eb_Gb :
    case OP_SUB_Ev_Gv :
    case OP_SUB_Gb_Eb :
    case OP_SUB_Gv_Ev :
    case OP_SUB_AL_Ib :
    case OP_SUB_AX_Iv :
    case OP_PREFIX_CS :
    case OP_XOR_Eb_Gb :
    case OP_XOR_Ev_Gv :
    case OP_XOR_Gb_Eb :
    case OP_XOR_Gv_Ev :
    case OP_XOR_AL_Ib :
    case OP_XOR_AX_Iv :
    case OP_PREFIX_SS :
    case OP_CMP_Eb_Gb :
    case OP_CMP_Ev_Gv :
    case OP_CMP_Gb_Eb :
    case OP_CMP_Gv_Ev :
    case OP_CMP_AL_Ib :
    case OP_CMP_AX_Iv :
    case OP_PREFIX_DS :
    case OP_INC_AX    :
    case OP_INC_CX    :
    case OP_INC_DX    :
    case OP_INC_BX    :
    case OP_INC_SP    :
    case OP_INC_BP    :
    case OP_INC_SI    :
    case OP_INC_DI    :
    case OP_DEC_AX    :
    case OP_DEC_CX    :
    case OP_DEC_DX    :
    case OP_DEC_BX    :
    case OP_DEC_SP    :
    case OP_DEC_BP    :
    case OP_DEC_SI    :
    case OP_DEC_DI    :
    case OP_PUSH_AX   :
    case OP_PUSH_CX   :
    case OP_PUSH_DX   :
    case OP_PUSH_BX   :
    case OP_PUSH_SP   :
    case OP_PUSH_BP   :
    case OP_PUSH_SI   :
    case OP_PUSH_DI   :
    case OP_POP_AX    :
    case OP_POP_CX    :
    case OP_POP_DX    :
    case OP_POP_BX    :
    case OP_POP_SP    :
    case OP_POP_BP    :
    case OP_POP_SI    :
    case OP_POP_DI    :
    case OP_JO_Jb     :
    case OP_JNO_Jb    :
    case OP_JB_Jb     :
    case OP_JNB_Jb    :
    case OP_JZ_Jb     :
    case OP_JNZ_Jb    :
    case OP_JBE_Jb    :
    case OP_JA_Jb     :
    case OP_JS_Jb     :
    case OP_JNS_Jb    :
    case OP_JPE_Jb    :
    case OP_JPO_Jb    :
    case OP_JL_Jb     :
    case OP_JGE_Jb    :
    case OP_JLE_Jb    :
    case OP_JG_Jb     :
    case OP_TEST_Gb_Eb:
    case OP_TEST_Gv_Ev:
    case OP_XCHG_Gb_Eb:
    case OP_XCHG_Gv_Ev:
    case OP_MOV_Eb_Gb :
    case OP_MOV_Ev_Gv :
    case OP_MOV_Gb_Eb :
    case OP_MOV_Gv_Ev :
    case OP_MOV_Ew_Sw :
    case OP_LEA_Gv_M  :
    case OP_MOV_Sw_Ew :
    case OP_POP_Ev    :
    case OP_NOP       :
    case OP_XCHG_CX_AX:
    case OP_XCHG_DX_AX:
    case OP_XCHG_BX_AX:
    case OP_XCHG_SP_AX:
    case OP_XCHG_BP_AX:
    case OP_XCHG_SI_AX:
    case OP_XCHG_DI_AX:
    case OP_CBW       :
    case OP_CWD       :
    case OP_CALL_Ap   :
    case OP_WAIT      :
    case OP_PUSHF     :
    case OP_POPF      :
    case OP_SAHF      :
    case OP_LAHF      :
    case OP_MOV_AL_Ob :
    case OP_MOV_AX_Ov :
    case OP_MOV_Ob_AL :
    case OP_MOV_Ov_AX :
    case OP_MOVSB     :
    case OP_MOVSW     :
    case OP_CMPSB     :
    case OP_CMPSW     :
    case OP_TEST_AL_Ib:
    case OP_TEST_AX_Iv:
    case OP_STOSB     :
    case OP_STOSW     :
    case OP_LODSB     :
    case OP_LODSW     :
    case OP_SCASB     :
    case OP_SCASW     :
    case OP_MOV_AL_Ib :
    case OP_MOV_CL_Ib :
    case OP_MOV_DL_Ib :
    case OP_MOV_BL_Ib :
    case OP_MOV_AH_Ib :
    case OP_MOV_CH_Ib :
    case OP_MOV_DH_Ib :
    case OP_MOV_BH_Ib :
    case OP_MOV_AX_Iv :
    case OP_MOV_CX_Iv :
    case OP_MOV_DX_Iv :
    case OP_MOV_BX_Iv :
    case OP_MOV_SP_Iv :
    case OP_MOV_BP_Iv :
    case OP_MOV_SI_Iv :
    case OP_MOV_DI_Iv :
    case OP_RET_Iw    :
    case OP_RET       :
    case OP_LES_Gv_Mp :
    case OP_LDS_Gv_Mp :
    case OP_MOV_Eb_Ib :
    case OP_MOV_Ev_Iv :
    case OP_RETF_Iw   :
    case OP_RETF      :
    case OP_INT_3     :
    case OP_INT_Ib    :
    case OP_INTO      :
    case OP_IRET      :
    case OP_XLAT      :
    case OP_LOOPNZ_Jb :
    case OP_LOOPZ_Jb  :
    case OP_LOOP_Jb   :
    case OP_JCXZ_Jb   :
    case OP_IN_AL_Ib  :
    case OP_IN_AX_Ib  :
    case OP_OUT_Ib_AL :
    case OP_OUT_Ib_AX :
    case OP_CALL_Jv   :
    case OP_JMP_Jv    :
    case OP_JMP_Ap    :
    case OP_JMP_Jb    :
    case OP_IN_AL_DX  :
    case OP_IN_AX_DX  :
    case OP_OUT_DX_AL :
    case OP_OUT_DX_AX :
    case OP_LOCK      :
    case OP_REPNZ     : // REPNE
    case OP_REPZ      : // REP, REPE
    case OP_HLT       :
    case OP_CMC       :
    case OP_CLC       :
    case OP_STC       :
    case OP_CLI       :
    case OP_STI       :
    case OP_CLD       :
    case OP_STD       :
    default: unknown("dispatch");
    }
}

void Cpu_8086::unknown(const string &stage) {
    throw CpuError("Unknown opcode at "s + stage + " stage, address "s + csip().toString() + ": " + hexVal(opcode_));
}

void Cpu_8086::instr_mov() {
    Register r_src, r_dst;
    switch (opcode_) {
    case OP_MOV_Eb_Gb: // mov [bp+si],ah
        modrmParse();
        r_src = modrmRegRegister(REG_GP8);
        r_dst = modrmMemRegister(REG_GP8);
        if (r_dst != REG_NONE) reg8(r_dst) = reg8(r_src);
        else mem_->writeByte(modrmMemAddress(), reg8(r_src));
        ipAdvance(modrmInstructionLength());
        break;
    case OP_MOV_Ev_Gv:
    case OP_MOV_Gb_Eb:
    case OP_MOV_Gv_Ev:
    case OP_MOV_Ew_Sw:
    case OP_MOV_Sw_Ew:
    case OP_MOV_AL_Ob:
    case OP_MOV_AX_Ov:
    case OP_MOV_Ob_AL:
    case OP_MOV_Ov_AX:
    case OP_MOV_AL_Ib:
    case OP_MOV_CL_Ib:
    case OP_MOV_DL_Ib:
    case OP_MOV_BL_Ib:
    case OP_MOV_AH_Ib:
    case OP_MOV_CH_Ib:
    case OP_MOV_DH_Ib:
    case OP_MOV_BH_Ib:
    case OP_MOV_AX_Iv:
    case OP_MOV_CX_Iv:
    case OP_MOV_DX_Iv:
    case OP_MOV_BX_Iv:
    case OP_MOV_SP_Iv:
    case OP_MOV_BP_Iv:
    case OP_MOV_SI_Iv:
    case OP_MOV_DI_Iv:
    case OP_MOV_Eb_Ib:
    case OP_MOV_Ev_Iv:
    default: 
        throw CpuError("Unsupported opcode for MOV: " + hexVal(opcode_));
    }

}