#ifndef CPU_H
#define CPU_H

#include <string>
#include <queue>
#include "dos/types.h"
#include "dos/registers.h"
#include "dos/memory.h"
#include "dos/analysis.h"
#include "dos/instruction.h"

class Cpu {
public:
    virtual std::string type() const = 0;
    virtual std::string info() const = 0;
    virtual void init(const Address &code, const Address &stack, const Size codeSize) = 0;
    virtual void step() = 0;
    virtual void run() = 0;
};

class InterruptInterface;

class Cpu_8086 : public Cpu {
    friend class CpuTest;
    
private:
    Memory *mem_;
    InterruptInterface *int_;
    Registers regs_;
    const Byte *memBase_, *code_;
    Byte opcode_, modrm_;
    Register segOverride_;
    Byte byteOperand1_, byteOperand2_, byteResult_;
    Word wordOperand1_, wordOperand2_, wordResult_;
    Block codeExtents_;
    bool done_, step_;

public:
    Cpu_8086(Memory *memory, InterruptInterface *inthandler);
    std::string type() const override { return "8086"; };
    std::string info() const override;
    void init(const Address &codeAddr, const Address &stackAddr, const Size codeSize) override;
    void step() override;
    void run() override;

private:
    // utility
    Register defaultSeg(const Register) const;
    void setCodeSegment(const Word seg);

    // instruction pointer access and manipulation
    inline Byte ipByte(const Word offset = 0) const { return code_[regs_.bit16(REG_IP) + offset]; }
    inline Word ipWord(const Word offset = 0) const { return *WORD_PTR(code_, regs_.bit16(REG_IP) + offset); }
    inline void ipJump(const Address &target) { setCodeSegment(target.segment); regs_.bit16(REG_IP) = target.offset; }
    inline void ipAdvance(const SWord amount) { regs_.bit16(REG_IP) += amount; }
    inline void ipAdvance(const SByte amount) { regs_.bit16(REG_IP) += amount; }

    inline Byte memByte(const Offset offset) const { return memBase_[offset]; }
    inline Word memWord(const Offset offset) const { return *WORD_PTR(memBase_, offset); }

    // ModR/M byte and evaluation
    Register modrmRegister(const RegType type, const Byte value) const;
    inline Register modrmRegRegister(const RegType regType) const;
    inline Register modrmMemRegister(const RegType regType) const;
    Offset modrmMemAddress() const;
    void modrmStoreByte(const Byte value);
    void modrmStoreWord(const Word value);
    Byte modrmGetByte();
    Word modrmGetWord();
    size_t modrmDisplacementLength() const;

    std::string opcodeStr() const;
    void preProcessOpcode();
    size_t instructionLength() const;

    // instruction execution pipeline
    void pipeline();
    void dispatch();
    void groupDispatch();
    void unknown(const std::string &stage) const;
    void updateFlags();

    // instructions 
    void instr_mov();
    void instr_int();
    void instr_cmp();
    void instr_sub();
    void instr_add();
    void instr_or();
    void instr_adc();
    void instr_sbb();
    void instr_and();
    void instr_xor();
    void instr_rol();
    void instr_ror();
    void instr_rcl();
    void instr_rcr();
    void instr_shl();
    void instr_shr();
    void instr_sar();
    void instr_test();
    void instr_not(); 
    void instr_neg(); 
    void instr_mul(); 
    void instr_imul();
    void instr_div(); 
    void instr_idiv();    
    void instr_inc();
    void instr_dec();
    void instr_call();
    void instr_jmp();
    void instr_push();
    void instr_pop();
    void instr_daa();
    void instr_das();
    void instr_aaa();
    void instr_aas();
    void instr_xchg();
    void instr_lea();
    void instr_nop();
    void instr_cbw();
    void instr_cwd();
    void instr_wait();
    void instr_pushf();
    void instr_popf();
    void instr_sahf();
    void instr_lahf();
    void instr_movsb();
    void instr_movsw();
    void instr_cmpsb();
    void instr_cmpsw();
    void instr_stosb();
    void instr_stosw();
    void instr_lodsb();
    void instr_lodsw();
    void instr_scasb();
    void instr_scasw();
    void instr_ret();
    void instr_les();
    void instr_lds();
    void instr_retf();
    void instr_iret();
    void instr_aam();
    void instr_aad();
    void instr_xlat();
    void instr_loopnz();
    void instr_loopz();
    void instr_loop(); 
    void instr_in();
    void instr_out();
    void instr_lock();
    void instr_repnz();
    void instr_repz();
    void instr_hlt();
    void instr_cmc();
    void instr_clc();
    void instr_stc();
    void instr_cli();
    void instr_sti();
    void instr_cld();
    void instr_std();

};

#endif // CPU_H
