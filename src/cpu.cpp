#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>
#include <bitset>

#include "dos/cpu.h"
#include "dos/opcodes.h"
#include "dos/modrm.h"
#include "dos/interrupt.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

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
    opcode_(OP_NOP), modrm_(0),
    byteOperand1_(0), byteOperand2_(0), byteResult_(0),
    wordOperand1_(0), wordOperand2_(0), wordResult_(0),
    done_(false), step_(false) {
    memBase_ = mem_->base();
    regs_.reset();
}

std::string Cpu_8086::info() const {
    return regs_.dump();
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

// convert a ModR/M register value (from either the REG or the MEM field of the ModR/M byte) to our internal register identifier
Register Cpu_8086::modrmRegister(const RegType type, const Byte value) const {
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
    switch (type) {
    // convert the REG value into an index into one of the above values by bit-shifting
    case REG_GP8:  return  modrm_regs8[value >> MODRM_REG_SHIFT];
    case REG_GP16: return modrm_regs16[value >> MODRM_REG_SHIFT];
    case REG_SEG:  return modrm_segreg[value >> MODRM_REG_SHIFT];
    default:
        throw CpuError("Invalid register type for ModR/M conversion: "s + to_string(type));
    }
}

// return id of register represented by the REG part of the ModR/M byte
inline Register Cpu_8086::modrmRegRegister(const RegType regType) const {
    return modrmRegister(regType, modrm_reg(modrm_));
}

// if the MEM part of the ModR/M byte actually represents a register (because MOD is 0b11), return its id, or invalid id otherwise
inline Register Cpu_8086::modrmMemRegister(const RegType regType) const {
    // if the MOD field of the current ModR/M value indicates MEM is really a REG, 
    // convert the MEM value to a REG value by bit-shifting, and find the corresponding register ID,
    // otherwise return REG_NONE
    return (modrm_mod(modrm_) == MODRM_MOD_REG) ? modrmRegister(regType, modrm_mem(modrm_) << MODRM_REG_SHIFT) : REG_NONE;
}

// calculate memory address to be used for the MEM operand of a ModR/M instruction, 
// the value of the MEM bits together with the MOD bits indicates the formula to use when calculating the address
Offset Cpu_8086::modrmMemAddress() const {
    Register baseReg;
    // obtain byte/word offset value for the displacement addressing modes, if applicable
    Offset displacement, address;
    switch (modrm_mod(modrm_)) {
    // assuming cs:ip points at the opcode, so the offset will be 2 bytes past that,
    // after the opcode and the modrm byte itself
    case MODRM_MOD_DISP8:  displacement = ipByte(2); break; 
    case MODRM_MOD_DISP16: displacement = ipWord(2); break;
    }
    // calculate address of the word or byte from the register values and optionally the displacement
    switch (modrm_mod(modrm_)) {
    // no displacement mode
    case MODRM_MOD_NODISP: 
        switch (modrm_mem(modrm_)) {
        case MODRM_MEM_BX_SI: baseReg = REG_BX;   address = regs_.bit16(REG_BX) + regs_.bit16(REG_SI); break;
        case MODRM_MEM_BX_DI: baseReg = REG_BX;   address = regs_.bit16(REG_BX) + regs_.bit16(REG_DI); break;
        case MODRM_MEM_BP_SI: baseReg = REG_BP;   address = regs_.bit16(REG_BP) + regs_.bit16(REG_SI); break;
        case MODRM_MEM_BP_DI: baseReg = REG_BP;   address = regs_.bit16(REG_BP) + regs_.bit16(REG_DI); break;
        case MODRM_MEM_SI:    baseReg = REG_SI;   address = regs_.bit16(REG_SI); break;
        case MODRM_MEM_DI:    baseReg = REG_DI;   address = regs_.bit16(REG_DI); break;
        case MODRM_MEM_ADDR:  baseReg = REG_NONE; address = ipWord(2); break; // address is the word past the opcode and modrm byte itself
        case MODRM_MEM_BX:    baseReg = REG_BX;   address = regs_.bit16(REG_BX); break;
        }
        break;
    // 8- and 16-bit displacement modes
    case MODRM_MOD_DISP8: // fall through
    case MODRM_MOD_DISP16:
        switch (modrm_mem(modrm_)) {
        case MODRM_MEM_BX_SI_OFF: baseReg = REG_BX; address = regs_.bit16(REG_BX) + regs_.bit16(REG_SI) + displacement; break;
        case MODRM_MEM_BX_DI_OFF: baseReg = REG_BX; address = regs_.bit16(REG_BX) + regs_.bit16(REG_DI) + displacement; break;
        case MODRM_MEM_BP_SI_OFF: baseReg = REG_BP; address = regs_.bit16(REG_BP) + regs_.bit16(REG_SI) + displacement; break;
        case MODRM_MEM_BP_DI_OFF: baseReg = REG_BP; address = regs_.bit16(REG_BP) + regs_.bit16(REG_DI) + displacement; break;
        case MODRM_MEM_SI_OFF:    baseReg = REG_SI; address = regs_.bit16(REG_SI) + displacement; break;
        case MODRM_MEM_DI_OFF:    baseReg = REG_DI; address = regs_.bit16(REG_DI) + displacement; break;
        case MODRM_MEM_BP_OFF:    baseReg = REG_BP; address = regs_.bit16(REG_BP) + displacement; break;
        case MODRM_MEM_BX_OFF:    baseReg = REG_BX; address = regs_.bit16(REG_BX) + displacement; break;
        }
        break;
    default:
        // can't calculate memory address for a register (MODRM_REG)
        throw CpuError("Invalid ModR/M for address calculation: "s + hexVal(modrm_mod(modrm_)));
    }
    // the calculated address is relative to a segment that is implicitly associated with the base register
    // TODO: implement segment override prefix which changes the default segment
    Offset segAddress = SEG_OFFSET(regs_.bit16(defaultSeg(baseReg)));
    return segAddress + address;
}

// calculate length of instruction with modrm byte
Word Cpu_8086::modrmInstructionLength() const {
    const Word length = 2; // at least opcode + modrm byte
    switch (modrm_mod(modrm_)) {
    case MODRM_MOD_NODISP:
        switch (modrm_mem(modrm_)) {
        // opcode + modrm + direct address word
        case MODRM_MEM_ADDR: return length + 2;
        // opcode + modrm
        default: return length;
        }
    case MODRM_MOD_DISP8:
        // opcode + modrm + displacement byte
        return length + 1;
    case MODRM_MOD_DISP16:
        // opcode + modrm + displacement word
        return length + 2;
    case MODRM_MOD_REG:
        // opcode + modrm
        return length;   
    }
    throw CpuError("Invalid ModR/M MOD value while calculating instruction length: "s + hexVal(modrm_mod(modrm_)));
}

void Cpu_8086::init(const Address &codeAddr, const Address &stackAddr) {
    // initialize registers
    regs_.reset();
    regs_.bit16(REG_CS) = codeAddr.segment;
    regs_.bit16(REG_IP) = codeAddr.offset;
    regs_.bit16(REG_SS) = stackAddr.segment;
    regs_.bit16(REG_SP) = stackAddr.offset;
    const Offset codeLinearAddr = SEG_OFFSET(regs_.bit16(REG_CS));
    const Offset pspLinearAddr = codeLinearAddr - PSP_SIZE;
    Address pspAddr(pspLinearAddr);
    assert(pspAddr.offset == 0);
    regs_.bit16(REG_DS) = regs_.bit16(REG_ES) = pspAddr.segment;
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
    case OP_XCHG_Gv_Ev: unknown();
    case OP_MOV_Eb_Gb :
    case OP_MOV_Ev_Gv :
    case OP_MOV_Gb_Eb :
    case OP_MOV_Gv_Ev :
    case OP_MOV_Ew_Sw : instr_mov(); break;
    case OP_LEA_Gv_M  : unknown();
    case OP_MOV_Sw_Ew : instr_mov(); break;
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
    case OP_LAHF      : unknown();
    case OP_MOV_AL_Ob :
    case OP_MOV_AX_Ov :
    case OP_MOV_Ob_AL :
    case OP_MOV_Ov_AX : instr_mov(); break;
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
    case OP_SCASW     : unknown();
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
    case OP_MOV_DI_Iv : instr_mov(); break;
    case OP_RET_Iw    : 
    case OP_RET       :
    case OP_LES_Gv_Mp :
    case OP_LDS_Gv_Mp : unknown();
    case OP_MOV_Eb_Ib : 
    case OP_MOV_Ev_Iv : instr_mov(); break;
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
    default: unknown();
    }
}

void Cpu_8086::unknown() {
    throw CpuError("Unknown opcode at address "s + regs_.csip().toString() + ": " + hexVal(opcode_));
}

void Cpu_8086::instr_mov() {
    Register r_src, r_dst;
    switch (opcode_) {
    case OP_MOV_Eb_Gb:
        modrm_ = ipByte(1);
        r_src = modrmRegRegister(REG_GP8);
        r_dst = modrmMemRegister(REG_GP8);
        if (r_dst == REG_NONE) mem_->writeByte(modrmMemAddress(), regs_.bit8(r_src));
        else regs_.bit8(r_dst) = regs_.bit8(r_src);
        ipAdvance(modrmInstructionLength());
        break;
    case OP_MOV_Ev_Gv:
        modrm_ = ipByte(1);
        r_src = modrmRegRegister(REG_GP16);
        r_dst = modrmMemRegister(REG_GP16);
        if (r_dst == REG_NONE) mem_->writeWord(modrmMemAddress(), regs_.bit16(r_src));
        else regs_.bit16(r_dst) = regs_.bit16(r_src);
        ipAdvance(modrmInstructionLength());
        break;    
    case OP_MOV_Gb_Eb:
        modrm_ = ipByte(1);
        r_src = modrmMemRegister(REG_GP8);
        r_dst = modrmRegRegister(REG_GP8);
        if (r_src == REG_NONE) regs_.bit8(r_dst) = memByte(modrmMemAddress());
        else regs_.bit8(r_dst) = regs_.bit8(r_src);
        ipAdvance(modrmInstructionLength());
        break;
    case OP_MOV_Gv_Ev:
        modrm_ = ipByte(1);
        r_src = modrmMemRegister(REG_GP16);
        r_dst = modrmRegRegister(REG_GP16);
        if (r_src == REG_NONE) regs_.bit16(r_dst) = memWord(modrmMemAddress());
        else regs_.bit16(r_dst) = regs_.bit16(r_src);
        ipAdvance(modrmInstructionLength());
        break;    
    case OP_MOV_Ew_Sw:
        modrm_ = ipByte(1);
        r_src = modrmRegRegister(REG_SEG);
        r_dst = modrmMemRegister(REG_GP16);
        if (r_dst == REG_NONE) mem_->writeWord(modrmMemAddress(), regs_.bit16(r_src));
        else regs_.bit16(r_dst) = regs_.bit16(r_src);
        ipAdvance(modrmInstructionLength());
        break;
    case OP_MOV_Sw_Ew:
        modrm_ = ipByte(1);
        r_src = modrmMemRegister(REG_GP16);
        r_dst = modrmRegRegister(REG_SEG);
        if (r_src == REG_NONE) regs_.bit16(r_dst) = memWord(modrmMemAddress());
        else regs_.bit16(r_dst) = regs_.bit16(r_src);
        ipAdvance(modrmInstructionLength());
        break;  
    case OP_MOV_AL_Ob:
        regs_.bit8(REG_AL) = memByte(SEG_OFFSET(regs_.bit16(REG_DS)) + ipWord(1));
        ipAdvance(3);
        break;
    case OP_MOV_AX_Ov:
        regs_.bit16(REG_AX) = memWord(SEG_OFFSET(regs_.bit16(REG_DS)) + ipWord(1));
        ipAdvance(3);
        break;    
    case OP_MOV_Ob_AL:
        mem_->writeByte(SEG_OFFSET(regs_.bit16(REG_DS)) + ipWord(1), regs_.bit8(REG_AL));
        ipAdvance(3);
        break;    
    case OP_MOV_Ov_AX:
        mem_->writeWord(SEG_OFFSET(regs_.bit16(REG_DS)) + ipWord(1), regs_.bit16(REG_AX));
        ipAdvance(3);
        break;        
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