#ifndef CPU_H
#define CPU_H

#include <string>
#include "types.h"
#include "memory.h"

class Cpu {
public:
    virtual std::string type() const = 0;
    virtual std::string info() const = 0;
    virtual void reset(const SegmentedAddress &code, const SegmentedAddress &stack, const Word &pspSeg) = 0;
    virtual void exec() = 0;
};

class Cpu_8086 : public Cpu {
public:
    struct Regs {
        // main registers
        union { Reg16 AX; struct { Reg8 AL, AH; }; };
        union { Reg16 BX; struct { Reg8 BL, BH; }; };
        union { Reg16 CX; struct { Reg8 CL, CH; }; };
        union { Reg16 DX; struct { Reg8 DL, DH; }; };
        // index registers
        Reg16 SI, DI, BP, SP;
        // program counter
        Reg16 IP;
        // segment registers
        Reg16 CS, DS, SS, ES;
        // status register
        union { Reg16 FLAGS;
            struct {
                Reg8 CF  : 1; // carry
                Reg8 B1  : 1; // ----
                Reg8 PF  : 1; // parity
                Reg8 B3  : 1; // ----
                Reg8 AF  : 1; // auxiliary carry
                Reg8 B5  : 1; // ----
                Reg8 ZF  : 1; // zero
                Reg8 SF  : 1; // sign
                Reg8 TF  : 1; // trap
                Reg8 IF  : 1; // interrupt
                Reg8 DF  : 1; // direction
                Reg8 OF  : 1; // overflow
                Reg8 B12 : 1; // ----
                Reg8 B13 : 1; // ----
                Reg8 B14 : 1; // ----
                Reg8 B15 : 1; // ----
            };
        };
        Regs() : 
            AX(0), BX(0), CX(0), DX(0), 
            SI(0), DI(0), BP(0), SP(0), IP(0),
            CS(0), DS(0), SS(0), ES(0), FLAGS(0) {}
    } regs_;

private:
    Byte* const memBase_;
    bool done_;

public:
    Cpu_8086(Byte* memoryBase) : memBase_(memoryBase), done_(false) {}
    virtual std::string type() const override { return "8086"; };
    virtual std::string info() const;
    virtual void reset(const SegmentedAddress &code, const SegmentedAddress &stack, const Word &pspSeg);
    virtual void exec();
};

// stc: 0x3103 0011000100000011
// clc: 0x3102 0011000100000010

#endif // CPU_H
