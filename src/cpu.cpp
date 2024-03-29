#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cassert>
#include <vector>
#include <stack>

#include "dos/cpu.h"
#include "dos/psp.h"
#include "dos/opcodes.h"
#include "dos/modrm.h"
#include "dos/interrupt.h"
#include "dos/util.h"
#include "dos/error.h"
#include "dos/output.h"

using namespace std;

#define UNKNOWN_DISPATCH unknown("dispatch")
#define UNKNOWN_ILEN unknown("instr_length")

static void cpuMessage(const string &msg, const LogPriority pri = LOG_INFO) {
    output(msg, LOG_CPU, pri);
}

Cpu_8086::Cpu_8086(Memory *memory, InterruptInterface *inthandler) : 
    mem_(memory), int_(inthandler), 
    memBase_(nullptr), code_(nullptr),
    opcode_(OP_NOP), modrm_(0),
    segOverride_(REG_NONE),
    byteOperand1_(0), byteOperand2_(0), byteResult_(0),
    wordOperand1_(0), wordOperand2_(0), wordResult_(0),
    done_(false), step_(false)
{
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

void Cpu_8086::setCodeSegment(const Word seg) {
    const Offset codeLinearAddr = SEG_TO_OFFSET(seg);
    regs_.set(REG_CS, seg);
    // update pointer to current code segment data
    code_ = memBase_ + codeLinearAddr;
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
        case MODRM_MEM_BX_SI: baseReg = REG_BX;   offset = regs_.get(REG_BX) + regs_.get(REG_SI); break;
        case MODRM_MEM_BX_DI: baseReg = REG_BX;   offset = regs_.get(REG_BX) + regs_.get(REG_DI); break;
        case MODRM_MEM_BP_SI: baseReg = REG_BP;   offset = regs_.get(REG_BP) + regs_.get(REG_SI); break;
        case MODRM_MEM_BP_DI: baseReg = REG_BP;   offset = regs_.get(REG_BP) + regs_.get(REG_DI); break;
        case MODRM_MEM_SI:    baseReg = REG_SI;   offset = regs_.get(REG_SI); break;
        case MODRM_MEM_DI:    baseReg = REG_DI;   offset = regs_.get(REG_DI); break;
        case MODRM_MEM_ADDR:  baseReg = REG_NONE; offset = ipWord(2); break; // address is the word past the opcode and modrm byte
        case MODRM_MEM_BX:    baseReg = REG_BX;   offset = regs_.get(REG_BX); break;
        }
        break;
    // 8- and 16-bit displacement modes
    case MODRM_MOD_DISP8: // fall through
    case MODRM_MOD_DISP16:
        switch (modrm_mem(modrm_)) {
        case MODRM_MEM_BX_SI_OFF: baseReg = REG_BX; offset = regs_.get(REG_BX) + regs_.get(REG_SI); break;
        case MODRM_MEM_BX_DI_OFF: baseReg = REG_BX; offset = regs_.get(REG_BX) + regs_.get(REG_DI); break;
        case MODRM_MEM_BP_SI_OFF: baseReg = REG_BP; offset = regs_.get(REG_BP) + regs_.get(REG_SI); break;
        case MODRM_MEM_BP_DI_OFF: baseReg = REG_BP; offset = regs_.get(REG_BP) + regs_.get(REG_DI); break;
        case MODRM_MEM_SI_OFF:    baseReg = REG_SI; offset = regs_.get(REG_SI) ; break;
        case MODRM_MEM_DI_OFF:    baseReg = REG_DI; offset = regs_.get(REG_DI) ; break;
        case MODRM_MEM_BP_OFF:    baseReg = REG_BP; offset = regs_.get(REG_BP) ; break;
        case MODRM_MEM_BX_OFF:    baseReg = REG_BX; offset = regs_.get(REG_BX) ; break;
        }
        // signed arithmetic, displacement may be negative
        offset += displacement;
        break;
    default:
        // can't calculate memory address for a register (MODRM_REG)
        throw CpuError("Invalid ModR/M for address calculation: "s + hexVal(modrm_mod(modrm_)));
    }
    // the calculated address is relative to a segment that is implicitly associated with the base register
    const Offset segOffset = SEG_TO_OFFSET(regs_.get(defaultSeg(baseReg)));
    return segOffset + offset;
}

// TODO: merge this with getInstruction in the future?
void Cpu_8086::preProcessOpcode() {
    static const Register PREFIX_REGS[4] = { REG_ES, REG_CS, REG_SS, REG_DS };
    Word ipOffset = 0;
    // clear segment override in case it was active
    segOverride_ = REG_NONE;
    modrm_ = 0;
    // in case the current opcode is actually a segment override prefix, set a flag and fetch the actual opcode from the next byte
    if (opcodeIsSegmentPrefix(opcode_)) {
        const size_t index = (opcode_ - OP_PREFIX_ES) / 8; // first prefix opcodes is for ES and subsequent ones are multiples of 8
        segOverride_ = PREFIX_REGS[index]; 
        opcode_ = ipByte(1);
        ipOffset++;
        // fall through, need to process updated opcode
        // TODO: ip remains at override prefix, modrm read etc will be off?
    }
    // read modrm byte if opcode contains it
    if (opcodeIsModrm(opcode_)) {
        modrm_ = ipByte(1 + ipOffset);
    }
}

size_t Cpu_8086::instructionLength() const {
    return 0; // TODO: fix
}

void Cpu_8086::init(const Address &codeAddr, const Address &stackAddr, const Size codeSize) {
    // initialize registers
    regs_.reset();
    setCodeSegment(codeAddr.segment);
    regs_.set(REG_IP, codeAddr.offset);
    regs_.set(REG_SS, stackAddr.segment);
    regs_.set(REG_SP, stackAddr.offset);
    const Offset pspLinearAddr = SEG_TO_OFFSET(codeAddr.segment) - PSP_SIZE;
    Address pspAddr(pspLinearAddr);
    assert(pspAddr.offset == 0);
    regs_.set(REG_DS, pspAddr.segment);
    regs_.set(REG_ES, pspAddr.segment);
    codeExtents_ = Block({codeAddr.segment, 0}, Address(SEG_TO_OFFSET(codeAddr.segment) + codeSize));
}

void Cpu_8086::step() {
    step_ = true;
    pipeline();
}

void Cpu_8086::run() {
    step_ = false;
    pipeline();
}

string Cpu_8086::opcodeStr() const {
    string ret = regs_.csip().toString() + "  " + hexVal(opcode_) + "  ";
    switch(segOverride_) {
    case REG_ES: ret += "ES:"; break;
    case REG_CS: ret += "CS:"; break;
    case REG_SS: ret += "SS:"; break;
    case REG_DS: ret += "DS:"; break;
    }
    return ret + opcodeName(opcode_);
}

// main loop for evaluating and executing instructions
void Cpu_8086::pipeline() {
    done_ = false;
    while (!done_) {
        opcode_ = ipByte();
        preProcessOpcode();
        // evaluate instruction, apply side efects
        dispatch();
        ipAdvance(WORD_SIGNED(instructionLength()));
        if (step_) break;
    }
}

void Cpu_8086::dispatch() {
    if (!opcodeIsGroup(opcode_)) switch (instr_class(opcode_)) {
    case INS_ADD: instr_add(); break;
    case INS_PUSH: instr_push(); break;
    case INS_POP: instr_pop(); break;
    case INS_OR: instr_or(); break;
    case INS_ADC: instr_adc(); break;
    case INS_SBB: instr_sbb(); break;
    case INS_AND: instr_and(); break;
    case INS_DAA: instr_daa(); break;
    case INS_SUB: instr_sub(); break;
    case INS_DAS: instr_das(); break;
    case INS_XOR: instr_xor(); break;
    case INS_AAA: instr_aaa(); break;
    case INS_CMP: instr_cmp(); break;
    case INS_AAS: instr_aas(); break;
    case INS_INC: instr_inc(); break;
    case INS_DEC: instr_dec(); break;
    case INS_JMP: instr_jmp(); break; // all unconditional jumps:
    case INS_TEST: instr_test(); break;
    case INS_XCHG: instr_xchg(); break;
    case INS_MOV: instr_mov(); break;
    case INS_LEA: instr_lea(); break;
    case INS_NOP: instr_nop(); break;
    case INS_CBW: instr_cbw(); break;
    case INS_CWD: instr_cwd(); break;
    case INS_CALL: instr_call(); break;
    case INS_WAIT: instr_wait(); break;
    case INS_PUSHF: instr_pushf(); break;
    case INS_POPF: instr_popf(); break;
    case INS_SAHF: instr_sahf(); break;
    case INS_LAHF: instr_lahf(); break;
    case INS_MOVSB: instr_movsb(); break;
    case INS_MOVSW: instr_movsw(); break;
    case INS_CMPSB: instr_cmpsb(); break;
    case INS_CMPSW: instr_cmpsw(); break;
    case INS_STOSB: instr_stosb(); break;
    case INS_STOSW: instr_stosw(); break;
    case INS_LODSB: instr_lodsb(); break;
    case INS_LODSW: instr_lodsw(); break;
    case INS_SCASB: instr_scasb(); break;
    case INS_SCASW: instr_scasw(); break;
    case INS_RET: instr_ret(); break;
    case INS_LES: instr_les(); break;
    case INS_LDS: instr_lds(); break;
    case INS_RETF: instr_retf(); break;
    case INS_INT: instr_int(); break;
    case INS_INTO: instr_int(); break;
    case INS_IRET: instr_iret(); break;
    case INS_AAM: instr_aam(); break;
    case INS_AAD: instr_aad(); break;
    case INS_XLAT: instr_xlat(); break;
    case INS_LOOPNZ: instr_loopnz(); break;
    case INS_LOOPZ: instr_loopz(); break;
    case INS_LOOP: instr_loop(); break; 
    case INS_IN: instr_in(); break;
    case INS_OUT: instr_out(); break;
    case INS_LOCK: instr_lock(); break;
    case INS_REPNZ: instr_repnz(); break;
    case INS_REPZ: instr_repz(); break;
    case INS_HLT: instr_hlt(); break;
    case INS_CMC: instr_cmc(); break;
    case INS_CLC: instr_clc(); break;
    case INS_STC: instr_stc(); break;
    case INS_CLI: instr_cli(); break;
    case INS_STI: instr_sti(); break;
    case INS_CLD: instr_cld(); break;
    case INS_STD:  instr_std(); break;
    default:
        UNKNOWN_DISPATCH;
        break;
    }
    else groupDispatch();
}

void Cpu_8086::groupDispatch() {
    // after a group opcode, the modrm byte's REG bits take the alternative meaning of a 'group'
    // which denotes the type of the operation to be performed
    const Byte group_op = modrm_reg(modrm_);
    switch (opcode_) {
    case OP_GRP1_Eb_Ib : 
    case OP_GRP1_Ev_Iv :
    case OP_GRP1_Eb_Ib2: 
    case OP_GRP1_Ev_Ib : 
        switch (group_op) {
        case MODRM_GRP1_ADD: instr_add(); break;
        case MODRM_GRP1_OR : instr_or();  break;
        case MODRM_GRP1_ADC: instr_adc(); break;
        case MODRM_GRP1_SBB: instr_sbb(); break;
        case MODRM_GRP1_AND: instr_and(); break;
        case MODRM_GRP1_SUB: instr_sub(); break;
        case MODRM_GRP1_XOR: instr_xor(); break;
        case MODRM_GRP1_CMP: instr_cmp(); break;
        default: 
            throw CpuError("Unexpected group operation for opcode " + opcodeString(opcode_) + ": " + hexVal(group_op));
        }
        break;
    case OP_GRP2_Eb_1: 
    case OP_GRP2_Ev_1: 
    case OP_GRP2_Eb_CL: 
    case OP_GRP2_Ev_CL: 
        switch (group_op) {
        case MODRM_GRP2_ROL: instr_rol(); break;
        case MODRM_GRP2_ROR: instr_ror(); break;
        case MODRM_GRP2_RCL: instr_rcl(); break;
        case MODRM_GRP2_RCR: instr_rcr(); break;
        case MODRM_GRP2_SHL: instr_shl(); break;
        case MODRM_GRP2_SHR: instr_shr(); break;
        case MODRM_GRP2_SAR: instr_sar(); break;
        default: 
            throw CpuError("Unexpected group operation for opcode " + opcodeString(opcode_) + ": " + hexVal(group_op));
        }
        break;
    case OP_GRP3a_Eb: 
    case OP_GRP3b_Ev: 
        switch (group_op) {
        case MODRM_GRP3_TEST: instr_test(); break;
        case MODRM_GRP3_NOT : instr_not(); break;
        case MODRM_GRP3_NEG : instr_neg(); break;
        case MODRM_GRP3_MUL : instr_mul(); break;
        case MODRM_GRP3_IMUL: instr_imul(); break;
        case MODRM_GRP3_DIV : instr_div(); break;
        case MODRM_GRP3_IDIV: instr_idiv(); break;
        default: 
            throw CpuError("Unexpected group operation for opcode " + opcodeString(opcode_) + ": " + hexVal(group_op));
        }
        break;
    case OP_GRP4_Eb: 
        switch (group_op) {
        case MODRM_GRP4_INC: instr_inc(); break;
        case MODRM_GRP4_DEC: instr_dec(); break;
        default: 
            throw CpuError("Unexpected group operation for opcode " + opcodeString(opcode_) + ": " + hexVal(group_op));            
        }
        break;
    case OP_GRP5_Ev:
        switch (group_op) {
        case MODRM_GRP5_INC:     instr_inc();  break;
        case MODRM_GRP5_DEC:     instr_dec();  break;
        case MODRM_GRP5_CALL:    instr_call(); break;
        case MODRM_GRP5_CALL_Mp: instr_call(); break; 
        case MODRM_GRP5_JMP:     instr_jmp();  break;
        case MODRM_GRP5_JMP_Mp:  instr_jmp();  break;
        case MODRM_GRP5_PUSH:    instr_push(); break; 
        default: 
            throw CpuError("Unexpected group operation for opcode " + opcodeString(opcode_) + ": " + hexVal(group_op));
        }
        break;
    default: 
        throw CpuError("Unexpected opcode for group instruction: " + opcodeString(opcode_));
    }
}

void Cpu_8086::unknown(const string &stage) const {
    string msg = "Unknown opcode during "s + stage + " stage @ "s + regs_.csip().toString() + ": " + hexVal(opcode_);
    if (opcodeIsModrm(opcode_)) msg += ", modrm = " + hexVal(modrm_);
    throw CpuError(msg);
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
    case OP_SUB_Eb_Gb:
    case OP_SUB_Gb_Eb:
    case OP_SUB_AL_Ib:
    case OP_CMP_Eb_Gb:
    case OP_CMP_Gb_Eb:
    case OP_CMP_AL_Ib:
        // byte sub
        regs_.setFlag(FLAG_CARRY, byteOperand1_ < byteOperand2_);
        regs_.setFlag(FLAG_OVER, ((byteOperand1_ ^ byteOperand2_) & (byteOperand1_ ^ byteResult_)) & BYTE_SIGN);
        break;
    case OP_SUB_Ev_Gv:
    case OP_SUB_Gv_Ev:
    case OP_SUB_AX_Iv:
    case OP_CMP_Ev_Gv:
    case OP_CMP_Gv_Ev:
    case OP_CMP_AX_Iv:    
        // word sub
        regs_.setFlag(FLAG_CARRY, wordOperand1_ < wordOperand2_);
        regs_.setFlag(FLAG_OVER, ((wordOperand1_ ^ wordOperand2_) & (wordOperand1_ ^ wordResult_)) & WORD_SIGN);
        break;
    default:
        throw CpuError("Unexpected opcode in flag update stage 1: "s + hexVal(opcode_));
    }
    // update remaining arithmetic flags, these are simpler
    switch (opcode_)
    {
    case OP_ADD_AL_Ib: 
    case OP_SUB_Eb_Gb:
    case OP_SUB_Gb_Eb:
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
    case OP_SUB_Ev_Gv:
    case OP_SUB_Gv_Ev:
    case OP_SUB_AX_Iv:        
    case OP_CMP_Ev_Gv:
    case OP_CMP_Gv_Ev:
    case OP_CMP_AX_Iv:
        // word result
        regs_.setFlag(FLAG_ZERO, wordResult_ == 0);
        regs_.setFlag(FLAG_SIGN, wordResult_ & WORD_SIGN);
        regs_.setFlag(FLAG_AUXC, ((wordOperand1_ ^ wordOperand2_) ^ wordResult_) & NIBBLE_CARRY);
        regs_.setFlag(FLAG_PARITY, parity_lookup[wordResult_ & 0xff]);
        break;
    default:
        throw CpuError("Unexpected opcode in flag update stage 2: "s + hexVal(opcode_));
    }
}

void Cpu_8086::instr_mov() {
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

// TODO: do we need to do signed arithmetic?
void Cpu_8086::instr_cmp() {
    updateFlags();
}

void Cpu_8086::instr_sub() {
    // reuse CMP implementation for data load, result calculation and flag update
}

void Cpu_8086::instr_add() {
    throw CpuError("Opcode not implemented: ADD");
}

void Cpu_8086::instr_or() {
    throw CpuError("Opcode not implemented: OR");
}

void Cpu_8086::instr_adc() {
    throw CpuError("Opcode not implemented: ADC");
}

void Cpu_8086::instr_sbb() {
    throw CpuError("Opcode not implemented: SBB");
}

void Cpu_8086::instr_and() {
    throw CpuError("Opcode not implemented: AND");
}

void Cpu_8086::instr_xor() {
    throw CpuError("Opcode not implemented: XOR");
}

void Cpu_8086::instr_rol() {
    throw CpuError("Opcode not implemented: ROL");
}
void Cpu_8086::instr_ror() {
    throw CpuError("Opcode not implemented: ROR");
}
void Cpu_8086::instr_rcl() {
    throw CpuError("Opcode not implemented: RCL");
}
void Cpu_8086::instr_rcr() {
    throw CpuError("Opcode not implemented: RCR");
}
void Cpu_8086::instr_shl() {
    throw CpuError("Opcode not implemented: SHL");
}
void Cpu_8086::instr_shr() {
    throw CpuError("Opcode not implemented: SHR");
}
void Cpu_8086::instr_sar() {
    throw CpuError("Opcode not implemented: SAR");
}

void Cpu_8086::instr_test() {
    throw CpuError("Opcode not implemented: TEST");
}

void Cpu_8086::instr_not() {
    throw CpuError("Opcode not implemented: NOT");
}

void Cpu_8086::instr_neg() {
    throw CpuError("Opcode not implemented: NEG");
}

void Cpu_8086::instr_mul() {
    throw CpuError("Opcode not implemented: MUL");
}

void Cpu_8086::instr_imul() {
    throw CpuError("Opcode not implemented: IMUL");
}

void Cpu_8086::instr_div() {
    throw CpuError("Opcode not implemented: DIV");
}

void Cpu_8086::instr_idiv()  {
    throw CpuError("Opcode not implemented: IDIV");
}

void Cpu_8086::instr_inc() {
    throw CpuError("Opcode not implemented: INC");
}

void Cpu_8086::instr_dec() {
    throw CpuError("Opcode not implemented: DEC");
}

void Cpu_8086::instr_call() {
    throw CpuError("Opcode not implemented: CALL");
}

void Cpu_8086::instr_jmp() {
    byteOperand1_ = BYTE_SIGNED(ipByte(1));
    bool jump = false;
    switch (opcode_) {
    case OP_JO_Jb : jump = regs_.getFlag(FLAG_OVER); break;
    case OP_JNO_Jb: jump = !regs_.getFlag(FLAG_OVER); break;
    case OP_JB_Jb : jump = regs_.getFlag(FLAG_CARRY); break;
    case OP_JNB_Jb: jump = !regs_.getFlag(FLAG_CARRY); break;
    case OP_JZ_Jb : jump = regs_.getFlag(FLAG_ZERO); break;
    case OP_JNZ_Jb: jump = !regs_.getFlag(FLAG_ZERO); break;
    case OP_JBE_Jb: jump = regs_.getFlag(FLAG_CARRY) || regs_.getFlag(FLAG_ZERO); break;
    case OP_JA_Jb : jump = !(regs_.getFlag(FLAG_CARRY) || regs_.getFlag(FLAG_ZERO)); break;
    case OP_JS_Jb : jump = regs_.getFlag(FLAG_SIGN); break;
    case OP_JNS_Jb: jump = !regs_.getFlag(FLAG_SIGN); break;
    case OP_JPE_Jb: jump = regs_.getFlag(FLAG_PARITY); break;
    case OP_JPO_Jb: jump = !regs_.getFlag(FLAG_PARITY); break;
    case OP_JL_Jb : jump = (regs_.getFlag(FLAG_SIGN) != regs_.getFlag(FLAG_OVER)); break;
    case OP_JGE_Jb: jump = (regs_.getFlag(FLAG_SIGN) == regs_.getFlag(FLAG_OVER)); break;
    case OP_JLE_Jb: jump = (regs_.getFlag(FLAG_SIGN) != regs_.getFlag(FLAG_OVER)) || regs_.getFlag(FLAG_ZERO); break;
    case OP_JG_Jb : jump = (regs_.getFlag(FLAG_SIGN) == regs_.getFlag(FLAG_OVER)) && !regs_.getFlag(FLAG_ZERO); break;
    default: 
        throw CpuError("Unexpected opcode for short JMP: " + hexVal(opcode_));    
    }
    if (jump) ipAdvance(BYTE_SIGNED(byteOperand1_));
}

void Cpu_8086::instr_push() {
    throw CpuError("Opcode not implemented: PUSH");
}

void Cpu_8086::instr_pop() {
    throw CpuError("Opcode not implemented: POP");
}

void Cpu_8086::instr_daa() {
    throw CpuError("Opcode not implemented: DAA");
}

void Cpu_8086::instr_das() {
    throw CpuError("Opcode not implemented: DAS");
}

void Cpu_8086::instr_aaa() {
    throw CpuError("Opcode not implemented: AAA");
}

void Cpu_8086::instr_aas() {
    throw CpuError("Opcode not implemented: AAS");
}

void Cpu_8086::instr_xchg() {
    throw CpuError("Opcode not implemented: AAS");
}

void Cpu_8086::instr_lea() {
    throw CpuError("Opcode not implemented: AAS");
}

void Cpu_8086::instr_nop() {
    throw CpuError("Opcode not implemented: NOP");
}

void Cpu_8086::instr_cbw() {
    throw CpuError("Opcode not implemented: CBW");
}

void Cpu_8086::instr_cwd() {
    throw CpuError("Opcode not implemented: CWD");
}

void Cpu_8086::instr_wait() {
    throw CpuError("Opcode not implemented: WAIT");
}

void Cpu_8086::instr_pushf() {
    throw CpuError("Opcode not implemented: PUSHF");
}

void Cpu_8086::instr_popf() {
    throw CpuError("Opcode not implemented: POPF");
}

void Cpu_8086::instr_sahf() {
    throw CpuError("Opcode not implemented: SAHF");
}

void Cpu_8086::instr_lahf() {
    throw CpuError("Opcode not implemented: LAHF");
}

void Cpu_8086::instr_movsb() {
throw CpuError("Opcode not implemented: MOVSB");    
}
void Cpu_8086::instr_movsw() {
throw CpuError("Opcode not implemented: MOVSW");
}
void Cpu_8086::instr_cmpsb() {
throw CpuError("Opcode not implemented: CMPSB");
}
void Cpu_8086::instr_cmpsw() {
throw CpuError("Opcode not implemented: CMPSW");
}
void Cpu_8086::instr_stosb() {
throw CpuError("Opcode not implemented: STOSB");
}
void Cpu_8086::instr_stosw() {
throw CpuError("Opcode not implemented: STOSW");
}
void Cpu_8086::instr_lodsb() {
throw CpuError("Opcode not implemented: LODSB");
}
void Cpu_8086::instr_lodsw() {
throw CpuError("Opcode not implemented: LODSW");
}
void Cpu_8086::instr_scasb() {
throw CpuError("Opcode not implemented: SCASB");
}
void Cpu_8086::instr_scasw() {
throw CpuError("Opcode not implemented: SCASW");
}
void Cpu_8086::instr_ret() {
throw CpuError("Opcode not implemented: RET");
}
void Cpu_8086::instr_les() {
throw CpuError("Opcode not implemented: LES");
}
void Cpu_8086::instr_lds() {
throw CpuError("Opcode not implemented: LDS");
}
void Cpu_8086::instr_retf() {
throw CpuError("Opcode not implemented: RETF");
}
void Cpu_8086::instr_iret() {
throw CpuError("Opcode not implemented: IRET");    
}
void Cpu_8086::instr_aam() {
throw CpuError("Opcode not implemented: AAM");
}
void Cpu_8086::instr_aad() {
throw CpuError("Opcode not implemented: AAD");
}
void Cpu_8086::instr_xlat() {
throw CpuError("Opcode not implemented: XLAT");
}
void Cpu_8086::instr_loopnz() {
throw CpuError("Opcode not implemented: LOOPNZ");
}
void Cpu_8086::instr_loopz() {
throw CpuError("Opcode not implemented: LOOPZ");
}
void Cpu_8086::instr_loop() {
throw CpuError("Opcode not implemented: LOOP");
}
void Cpu_8086::instr_in() {
throw CpuError("Opcode not implemented: IN");
}
void Cpu_8086::instr_out() {
throw CpuError("Opcode not implemented: OUT");
}
void Cpu_8086::instr_lock() {
throw CpuError("Opcode not implemented: LOCK");
}
void Cpu_8086::instr_repnz() {
throw CpuError("Opcode not implemented: REPNZ");
}
void Cpu_8086::instr_repz() {
throw CpuError("Opcode not implemented: REPZ");
}
void Cpu_8086::instr_hlt() {
throw CpuError("Opcode not implemented: HLT");
}
void Cpu_8086::instr_cmc() {
throw CpuError("Opcode not implemented: CMC");
}
void Cpu_8086::instr_clc() {
throw CpuError("Opcode not implemented: CLC");
}
void Cpu_8086::instr_stc() {
throw CpuError("Opcode not implemented: STC");
}
void Cpu_8086::instr_cli() {
throw CpuError("Opcode not implemented: CLI");
}
void Cpu_8086::instr_sti() {
throw CpuError("Opcode not implemented: STI");
}
void Cpu_8086::instr_cld() {
throw CpuError("Opcode not implemented: CLD");
}
void Cpu_8086::instr_std() {
throw CpuError("Opcode not implemented: STD");
}
