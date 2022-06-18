#ifndef CPU_H
#define CPU_H

#include <string>
#include <queue>
#include "dos/types.h"
#include "dos/registers.h"
#include "dos/memory.h"

class Cpu {
public:
    virtual std::string type() const = 0;
    virtual std::string info() const = 0;
    virtual void init(const Address &code, const Address &stack, const Size codeSize) = 0;
    virtual void step() = 0;
    virtual void run() = 0;
    virtual bool analyze() = 0;
};

class InterruptInterface;

class Cpu_8086 : public Cpu {
    friend class Cpu_8086_Test;
    
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
    bool analyze() override;

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

    std::string disasm() const;
    void preProcessOpcode();
    size_t instructionLength() const;

    // instruction execution pipeline
    void pipeline();
    void dispatch();
    void unknown(const std::string &stage) const;
    void updateFlags();

    // instructions 
    void instr_mov();
    void instr_int();
    void instr_cmp();
};

#endif // CPU_H
