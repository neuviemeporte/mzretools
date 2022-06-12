#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>
#include <vector>
#include <map>

#include "dos/cpu.h"
#include "dos/psp.h"
#include "dos/opcodes.h"
#include "dos/modrm.h"
#include "dos/interrupt.h"
#include "dos/routine.h"
#include "dos/util.h"
#include "dos/error.h"

using namespace std;

#define UNKNOWN_DISPATCH unknown("dispatch")
#define UNKNOWN_ILEN unknown("instr_length")

static void cpuMessage(const string &msg) {
    cout << msg << endl;
}

Cpu_8086::Cpu_8086(Memory *memory, InterruptInterface *inthandler) : 
    mem_(memory), int_(inthandler), 
    memBase_(nullptr), code_(nullptr),
    opcode_(OP_NOP), modrm_(0),
    segOverride_(REG_NONE),
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
    // segment override prefix in effect, ignore default segment
    if (segOverride_ != REG_NONE) return segOverride_;
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
    Word offset;
    SWord displacement;
    switch (modrm_mod(modrm_)) {
    // assuming cs:ip points at the opcode, so the displacement will be 2 bytes past that,
    // after the opcode and the modrm byte
    case MODRM_MOD_DISP8:  displacement = BYTE_SIGNED(ipByte(2)); break; 
    case MODRM_MOD_DISP16: displacement = WORD_SIGNED(ipWord(2)); break;
    }
    // calculate offset of the word or byte from the register values and optionally the displacement
    switch (modrm_mod(modrm_)) {
    // no displacement mode
    case MODRM_MOD_NODISP: 
        switch (modrm_mem(modrm_)) {
        case MODRM_MEM_BX_SI: baseReg = REG_BX;   offset = regs_.bit16(REG_BX) + regs_.bit16(REG_SI); break;
        case MODRM_MEM_BX_DI: baseReg = REG_BX;   offset = regs_.bit16(REG_BX) + regs_.bit16(REG_DI); break;
        case MODRM_MEM_BP_SI: baseReg = REG_BP;   offset = regs_.bit16(REG_BP) + regs_.bit16(REG_SI); break;
        case MODRM_MEM_BP_DI: baseReg = REG_BP;   offset = regs_.bit16(REG_BP) + regs_.bit16(REG_DI); break;
        case MODRM_MEM_SI:    baseReg = REG_SI;   offset = regs_.bit16(REG_SI); break;
        case MODRM_MEM_DI:    baseReg = REG_DI;   offset = regs_.bit16(REG_DI); break;
        case MODRM_MEM_ADDR:  baseReg = REG_NONE; offset = ipWord(2); break; // address is the word past the opcode and modrm byte
        case MODRM_MEM_BX:    baseReg = REG_BX;   offset = regs_.bit16(REG_BX); break;
        }
        break;
    // 8- and 16-bit displacement modes
    case MODRM_MOD_DISP8: // fall through
    case MODRM_MOD_DISP16:
        switch (modrm_mem(modrm_)) {
        case MODRM_MEM_BX_SI_OFF: baseReg = REG_BX; offset = regs_.bit16(REG_BX) + regs_.bit16(REG_SI); break;
        case MODRM_MEM_BX_DI_OFF: baseReg = REG_BX; offset = regs_.bit16(REG_BX) + regs_.bit16(REG_DI); break;
        case MODRM_MEM_BP_SI_OFF: baseReg = REG_BP; offset = regs_.bit16(REG_BP) + regs_.bit16(REG_SI); break;
        case MODRM_MEM_BP_DI_OFF: baseReg = REG_BP; offset = regs_.bit16(REG_BP) + regs_.bit16(REG_DI); break;
        case MODRM_MEM_SI_OFF:    baseReg = REG_SI; offset = regs_.bit16(REG_SI) ; break;
        case MODRM_MEM_DI_OFF:    baseReg = REG_DI; offset = regs_.bit16(REG_DI) ; break;
        case MODRM_MEM_BP_OFF:    baseReg = REG_BP; offset = regs_.bit16(REG_BP) ; break;
        case MODRM_MEM_BX_OFF:    baseReg = REG_BX; offset = regs_.bit16(REG_BX) ; break;
        }
        // signed arithmetic, displacement may be negative
        offset += displacement;
        break;
    default:
        // can't calculate memory address for a register (MODRM_REG)
        throw CpuError("Invalid ModR/M for address calculation: "s + hexVal(modrm_mod(modrm_)));
    }
    // the calculated address is relative to a segment that is implicitly associated with the base register
    const Offset segOffset = SEG_OFFSET(regs_.bit16(defaultSeg(baseReg)));
    return segOffset + offset;
}

// write a byte to the location (memory or register) referenced by the MEM field in the ModR/M byte
void Cpu_8086::modrmStoreByte(const Byte value) {
    const Register regDest = modrmMemRegister(REG_GP8);
    if (regDest == REG_NONE) mem_->writeByte(modrmMemAddress(), value);
    else regs_.bit8(regDest) = value;
}

// write a word to the location (memory or register) referenced by the MEM field in the ModR/M byte
void Cpu_8086::modrmStoreWord(const Word value) {
    const Register regDest = modrmMemRegister(REG_GP16);
    if (regDest == REG_NONE) mem_->writeWord(modrmMemAddress(), value);
    else regs_.bit16(regDest) = value;
}

// read a byte from the location (memory or register) referenced by the MEM field in the ModR/M byte
Byte Cpu_8086::modrmGetByte() {
    const Register regSrc = modrmMemRegister(REG_GP8);
    if (regSrc == REG_NONE) return memByte(modrmMemAddress());
    else return regs_.bit8(regSrc);
}

// read a word from the location (memory or register) referenced by the MEM field in the ModR/M byte
Word Cpu_8086::modrmGetWord() {
    const Register regSrc = modrmMemRegister(REG_GP16);
    if (regSrc == REG_NONE) return memWord(modrmMemAddress());
    else return regs_.bit16(regSrc);
}

// length of modrm data following a modrm byte
Word Cpu_8086::modrmDisplacementLength() const {
    switch (modrm_mod(modrm_)) {
    case MODRM_MOD_NODISP:
        switch (modrm_mem(modrm_)) {
        // direct address word
        case MODRM_MEM_ADDR: return 2;
        // address from registers alone
        default: return 0;
        }
    case MODRM_MOD_DISP8:
        // displacement byte
        return 1;
    case MODRM_MOD_DISP16:
        // displacement word
        return 2;
    case MODRM_MOD_REG:
        // register
        return 0;   
    }
    throw CpuError("Invalid ModR/M MOD value while calculating instruction length: "s + hexVal(modrm_mod(modrm_)));
}

void Cpu_8086::preProcessOpcode() {
    static const Register PREFIX_REGS[4] = { REG_ES, REG_CS, REG_SS, REG_DS };
    Word ipOffset = 0;
    // in case the current opcode is actually a segment override prefix, set a flag and fetch the actual opcode from the next byte
    if (opcodeIsSementPrefix(opcode_)) {
        const size_t index = (opcode_ - OP_PREFIX_ES) / 8; // first prefix opcodes is for ES and subsequent ones are multiples of 8
        segOverride_ = PREFIX_REGS[index]; 
        opcode_ = ipByte(1);
        ipOffset++;
    }
    // read modrm byte if opcode contains it
    if (opcodeIsModrm(opcode_)) {
        modrm_ = ipByte(1 + ipOffset);
    }
}

void Cpu_8086::postProcessOpcode() {
    // set instruction pointer to next instruction, 
    // this needs to be done before clearing the segOverride_ value
    // TODO: take care of jumps
    ipAdvance(instructionLength());
    // clear segment override for next instruction in case it was active,
    segOverride_ = REG_NONE;
}

// calculate total instruction length including optional prefix byte, the opcode, 
// optional ModR/M byte, its (optional) displacement and any (also optional) immediate data
Word Cpu_8086::instructionLength() const {
    Word length = opcodeInstructionLength(opcode_);
    // if opcode is followed by a modrm byte, that could in turn be followed by a displacement value
    if (opcodeIsModrm(opcode_)) length += modrmDisplacementLength();
    // one more byte for segment override prefix if active
    if (segOverride_ != REG_NONE) length += 1;
    return length;
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

string Cpu_8086::disasm() const {
    string ret = regs_.csip().toString() + "  " + hexVal(opcode_) + "  ";
    switch(segOverride_) {
    case REG_ES: ret += "ES:"; break;
    case REG_CS: ret += "CS:"; break;
    case REG_SS: ret += "SS:"; break;
    case REG_DS: ret += "DS:"; break;
    }
    return ret + opcodeName(opcode_);
}

void Cpu_8086::findReturn(queue<Address> &branches, queue<Address> &calls) {
    cpuMessage("Scanning opcodes starting at " + regs_.csip());
    Address destination;
    while (true) {
        opcode_ = ipByte();
        preProcessOpcode();
        //cpuMessage(disasm());
        switch (opcode_)
        // TODO: process GRP5 jumps and calls
        // TODO: ignore jumps and calls outside of load module boundaries?
        // TODO: unconditional JMP is like a return, can't gloss over it
        {
        // conditional jumps
        case OP_JO_Jb :
        case OP_JNO_Jb:
        case OP_JB_Jb :
        case OP_JNB_Jb:
        case OP_JZ_Jb :
        case OP_JNZ_Jb:
        case OP_JBE_Jb:
        case OP_JA_Jb :
        case OP_JS_Jb :
        case OP_JNS_Jb:
        case OP_JPE_Jb:
        case OP_JPO_Jb:
        case OP_JL_Jb :
        case OP_JGE_Jb:
        case OP_JLE_Jb:
        case OP_JG_Jb :
        case OP_JMP_Jb:
        case OP_JCXZ_Jb:
            destination = jumpDestination(BYTE_SIGNED(ipByte(1)));
            branches.push(destination);
            cpuMessage("\t"s + regs_.csip() + ": found branch to "s + destination);
            break;
        // call
        case OP_CALL_Ap:
            destination = { ipWord(1), ipWord(3) };
            calls.push(destination);
            cpuMessage("\t"s + regs_.csip() + ": found call to "s + destination);
            break;
        // return
        case OP_RET_Iw:
        case OP_RET   :
        case OP_RETF_Iw:
        case OP_RETF   :
        case OP_IRET   :
            cpuMessage("\t"s + regs_.csip() + ": found return");
            return;
        // loop
        case OP_LOOPNZ_Jb:
        case OP_LOOPZ_Jb :
        case OP_LOOP_Jb  :
            destination = jumpDestination(BYTE_SIGNED(ipByte(1)));
            branches.push(destination);
            cpuMessage("\t"s + regs_.csip() + ": found loop to "s + destination);
            break;
        // call
        case OP_CALL_Jv:
            destination = jumpDestination(WORD_SIGNED(ipWord(1)));
            calls.push(destination);
            cpuMessage("\t"s + regs_.csip() + ": found call to "s + destination);
            break;
        // unconditional jump
        case OP_JMP_Jv:
            destination = jumpDestination(WORD_SIGNED(ipWord(1)));
            branches.push(destination);
            cpuMessage("\t"s + regs_.csip() + ": found branch to "s + destination);
            break;
        case OP_JMP_Ap:
            destination = { ipWord(1), ipWord(3) };
            branches.push(destination);
            cpuMessage("\t"s + regs_.csip() + ": found branch to "s + destination);
            break;        
        } // switch on opcode
        postProcessOpcode();
    } // iterate over opcodes
}

// explore the code without actually executing instructions, discover routine boundaries
void Cpu_8086::analyze() {
    done_ = false;
    // iterate over opcodes in the current routine
    queue<Address> calls;
    Address destination;
    Routine routine{"start"};
    routine.extents = Block{regs_.csip()};
    queue<Address> branches;
    // find end of routine extent, make note of any jumps and calls
    findReturn(branches, calls);
    routine.extents.end = regs_.csip();
    // process all branches found while scanning routine
    cpuMessage("Found routine '" + routine.name + ": " + routine.extents + ", " + to_string(branches.size()) + " branches, " + to_string(calls.size()) + " calls");
    while (!branches.empty()) {
        const Address branch = branches.front();
        branches.pop();
        if (routine.contains(branch)) {
            cpuMessage("\t"s + branch + ": ignoring branch within routine");
            continue;
        }
        // otherwise create a function chunk
        Block chunkExtents = Block{branch};
        ipJump(branch);
        findReturn(branches, calls);
        chunkExtents.end = regs_.csip();
        routine.chunks.push_back(chunkExtents);
    } 
}

// main loop for evaluating and executing instructions
void Cpu_8086::pipeline() {
    done_ = false;
    while (!done_) {
        opcode_ = ipByte();
        preProcessOpcode();
        // evaluate instruction, apply side efects
        dispatch();
        postProcessOpcode();
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
    case OP_SUB_Eb_Gb :
    case OP_SUB_Ev_Gv :
    case OP_SUB_Gb_Eb :
    case OP_SUB_Gv_Ev :
    case OP_SUB_AL_Ib :
    case OP_SUB_AX_Iv :
    case OP_XOR_Eb_Gb :
    case OP_XOR_Ev_Gv :
    case OP_XOR_Gb_Eb :
    case OP_XOR_Gv_Ev :
    case OP_XOR_AL_Ib :
    case OP_XOR_AX_Iv : UNKNOWN_DISPATCH;
    case OP_CMP_Eb_Gb : 
    case OP_CMP_Ev_Gv :
    case OP_CMP_Gb_Eb :
    case OP_CMP_Gv_Ev :
    case OP_CMP_AL_Ib :
    case OP_CMP_AX_Iv : instr_cmp(); break;
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
    case OP_XCHG_Gv_Ev: UNKNOWN_DISPATCH;
    case OP_MOV_Eb_Gb :
    case OP_MOV_Ev_Gv :
    case OP_MOV_Gb_Eb :
    case OP_MOV_Gv_Ev :
    case OP_MOV_Ew_Sw : instr_mov(); break;
    case OP_LEA_Gv_M  : UNKNOWN_DISPATCH;
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
    case OP_LAHF      : UNKNOWN_DISPATCH;
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
    case OP_SCASW     : UNKNOWN_DISPATCH;
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
    case OP_LDS_Gv_Mp : UNKNOWN_DISPATCH;
    case OP_MOV_Eb_Ib : 
    case OP_MOV_Ev_Iv : instr_mov(); break;
    case OP_RETF_Iw   :
    case OP_RETF      : UNKNOWN_DISPATCH;
    case OP_INT_3     : 
    case OP_INT_Ib    :
    case OP_INTO      : instr_int(); break;
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
    default: UNKNOWN_DISPATCH;
    }
}

void Cpu_8086::unknown(const string &stage) const {
    throw CpuError("Unknown opcode during "s + stage + " stage @ "s + regs_.csip().toString() + ": " + hexVal(opcode_));
}

void Cpu_8086::updateFlags() {
    static const Byte NIBBLE_CARRY = 0x10;
    static const Byte BYTE_SIGN = 0x80;
    static const Word WORD_SIGN = 0x8000;
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
    // update carry and overflow
    switch (opcode_) {
    case OP_ADD_AL_Ib: 
        // byte add
        regs_.setFlag(FLAG_CARRY, byteResult_ < byteOperand1_);
        regs_.setFlag(FLAG_OVER, ((byteOperand1_ ^ byteOperand2_ ^ BYTE_SIGN) & (byteResult_ ^ byteOperand2_)) & BYTE_SIGN);
        break;
    case OP_SUB_AL_Ib:
    case OP_CMP_Eb_Gb:
    case OP_CMP_Gb_Eb:
    case OP_CMP_AL_Ib:
        // byte sub
        regs_.setFlag(FLAG_CARRY, byteOperand1_ < byteOperand2_);
        regs_.setFlag(FLAG_OVER, ((byteOperand1_ ^ byteOperand2_) & (byteOperand1_ ^ byteResult_)) & BYTE_SIGN);
        break;
    case OP_CMP_Ev_Gv:
    case OP_CMP_Gv_Ev:
    case OP_CMP_AX_Iv:    
        // word sub
        regs_.setFlag(FLAG_CARRY, wordOperand1_ < wordOperand2_);
        regs_.setFlag(FLAG_OVER, ((wordOperand1_ ^ wordOperand2_) & (wordOperand1_ ^ wordResult_)) & WORD_SIGN);
    default:
        throw CpuError("Unexpected opcode in flag update stage 1: "s + hexVal(opcode_));
    }
    // update remaining arithmetic flags, these are simpler
    switch (opcode_)
    {
    case OP_ADD_AL_Ib: 
    case OP_SUB_AL_Ib:
    case OP_CMP_Eb_Gb:
    case OP_CMP_Gb_Eb:    
    case OP_CMP_AL_Ib:
        // byte result
        regs_.setFlag(FLAG_ZERO, byteResult_ == 0);
        regs_.setFlag(FLAG_SIGN, byteResult_ & BYTE_SIGN);
        regs_.setFlag(FLAG_AUXC, ((byteOperand1_ ^ byteOperand2_) ^ byteResult_) & NIBBLE_CARRY);
        regs_.setFlag(FLAG_PARITY, parity_lookup[byteResult_]);
        break;
    case OP_CMP_Ev_Gv:
    case OP_CMP_Gv_Ev:
    case OP_CMP_AX_Iv:
        // word result
        regs_.setFlag(FLAG_ZERO, wordResult_ == 0);
        regs_.setFlag(FLAG_SIGN, wordResult_ & WORD_SIGN);
        regs_.setFlag(FLAG_AUXC, ((wordOperand1_ ^ wordOperand2_) ^ wordResult_) & NIBBLE_CARRY);
        regs_.setFlag(FLAG_PARITY, parity_lookup[wordResult_ & 0xff]);    
    default:
        throw CpuError("Unexpected opcode in flag update stage 2: "s + hexVal(opcode_));
    }
}

void Cpu_8086::instr_mov() {
    switch (opcode_) {
    case OP_MOV_Eb_Gb:
        modrmStoreByte(regs_.bit8(modrmRegRegister(REG_GP8)));
        break;
    case OP_MOV_Ev_Gv:
        modrmStoreWord(regs_.bit16(modrmRegRegister(REG_GP16)));
        break;    
    case OP_MOV_Gb_Eb:
        regs_.bit8(modrmRegRegister(REG_GP8)) = modrmGetByte();
        break;
    case OP_MOV_Gv_Ev:
        regs_.bit16(modrmRegRegister(REG_GP16)) = modrmGetWord();
        break;    
    case OP_MOV_Ew_Sw:
        modrmStoreWord(regs_.bit16(modrmRegRegister(REG_SEG)));
        break;
    case OP_MOV_Sw_Ew:
        regs_.bit16(modrmRegRegister(REG_SEG)) = modrmGetWord();
        break;  
    case OP_MOV_AL_Ob:
        regs_.bit8(REG_AL) = memByte(SEG_OFFSET(regs_.bit16(REG_DS)) + ipWord(1));
        break;
    case OP_MOV_AX_Ov:
        regs_.bit16(REG_AX) = memWord(SEG_OFFSET(regs_.bit16(REG_DS)) + ipWord(1));
        break;    
    case OP_MOV_Ob_AL:
        mem_->writeByte(SEG_OFFSET(regs_.bit16(REG_DS)) + ipWord(1), regs_.bit8(REG_AL));
        break;    
    case OP_MOV_Ov_AX:
        mem_->writeWord(SEG_OFFSET(regs_.bit16(REG_DS)) + ipWord(1), regs_.bit16(REG_AX));
        break;        
    case OP_MOV_AL_Ib:
        regs_.bit8(REG_AL) = ipByte(1);
        break;
    case OP_MOV_CL_Ib:
        regs_.bit8(REG_CL) = ipByte(1);
        break;    
    case OP_MOV_DL_Ib:
        regs_.bit8(REG_DL) = ipByte(1);
        break;    
    case OP_MOV_BL_Ib:
        regs_.bit8(REG_BL) = ipByte(1);
        break;    
    case OP_MOV_AH_Ib:
        regs_.bit8(REG_AH) = ipByte(1);
        break;    
    case OP_MOV_CH_Ib:
        regs_.bit8(REG_CH) = ipByte(1);
        break;    
    case OP_MOV_DH_Ib:
        regs_.bit8(REG_DH) = ipByte(1);
        break;    
    case OP_MOV_BH_Ib:
        regs_.bit8(REG_BH) = ipByte(1);
        break;    
    case OP_MOV_AX_Iv:
        regs_.bit16(REG_AX) = ipWord(1);
        break;
    case OP_MOV_CX_Iv:
        regs_.bit16(REG_CX) = ipWord(1);
        break;    
    case OP_MOV_DX_Iv:
        regs_.bit16(REG_DX) = ipWord(1);
        break;    
    case OP_MOV_BX_Iv:
        regs_.bit16(REG_BX) = ipWord(1);
        break;    
    case OP_MOV_SP_Iv:
        regs_.bit16(REG_SP) = ipWord(1);
        break;    
    case OP_MOV_BP_Iv:
        regs_.bit16(REG_BP) = ipWord(1);
        break;    
    case OP_MOV_SI_Iv:
        regs_.bit16(REG_SI) = ipWord(1);
        break;    
    case OP_MOV_DI_Iv:
        regs_.bit16(REG_DI) = ipWord(1);
        break;    
    case OP_MOV_Eb_Ib:
        modrmStoreByte(ipByte(2 + modrmDisplacementLength()));
        break;
    case OP_MOV_Ev_Iv:
        modrmStoreWord(ipWord(2 + modrmDisplacementLength()));
        break;    
    default: 
        throw CpuError("Unexpected opcode for MOV: " + hexVal(opcode_));
    }
}

void Cpu_8086::instr_int() {
    Byte num;
    switch (opcode_) {
    case OP_INT_3:
        int_->interrupt(3, regs_);
        break;
    case OP_INT_Ib:
        num = ipByte(1);
        int_->interrupt(num, regs_);
        break;
    case OP_INTO:
        if (regs_.getFlag(FLAG_OVER)) int_->interrupt(4, regs_);
        break;    
    default: 
        throw CpuError("Unexpected opcode for INT: " + hexVal(opcode_));        
    }
}

void Cpu_8086::instr_cmp() {
    switch (opcode_) {
    case OP_CMP_Eb_Gb:
        byteOperand1_ = modrmGetByte();
        byteOperand2_ = regs_.bit8(modrmRegRegister(REG_GP8));
        byteResult_ = byteOperand1_ - byteOperand2_;
        updateFlags();
        break;
    case OP_CMP_Ev_Gv:
        wordOperand1_ = modrmGetWord();
        wordOperand2_ = regs_.bit16(modrmRegRegister(REG_GP16));
        wordResult_ = wordOperand1_ - wordOperand2_;
        updateFlags();
        break;    
    case OP_CMP_Gb_Eb:
        byteOperand1_ = regs_.bit8(modrmRegRegister(REG_GP8));
        byteOperand2_ = modrmGetByte();
        byteResult_ = byteOperand1_ - byteOperand2_;
        updateFlags();
        break;    
    case OP_CMP_Gv_Ev:
        wordOperand1_ = regs_.bit16(modrmRegRegister(REG_GP16));
        wordOperand2_ = modrmGetWord();
        wordResult_ = wordOperand1_ - wordOperand2_;
        updateFlags();
        break;        
    case OP_CMP_AL_Ib:
        byteOperand1_ = regs_.bit8(REG_AL);
        byteOperand2_ = ipByte(1);
        byteResult_ = byteOperand1_ - byteOperand2_;
        updateFlags();
        break;
    case OP_CMP_AX_Iv:
        wordOperand1_ = regs_.bit16(REG_AX);
        wordOperand2_ = ipWord(1);
        wordResult_ = wordOperand1_ - wordOperand2_;
        updateFlags();
        break;    
    default: 
        throw CpuError("Unexpected opcode for CMP: " + hexVal(opcode_));    
    }
}
