#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "dos/types.h"
#include "dos/memory.h"
class Dos;

enum IntStatus {
    INT_OK, INT_FAIL, INT_EXIT, INT_TERMINATE
};

class InterruptInterface {
public:
    virtual void setup(Byte *regs8, Word *regs16) = 0;
    virtual IntStatus interrupt(const Byte num) = 0;
};

// an adapter class for dispatching interrupt requests from the CPU to other system components,
// whose aim is to decouple the CPU from these components. This class is a friend of the Cpu class
// so it can set the CPU registers according to the contract of the various interrupt functions,
// without the Cpu needing to be aware of the components, or the components being aware of 
// and/or poking at the Cpu.
class InterruptHandler : public InterruptInterface {
protected:
    Dos *dos_;
    Byte* regs8_;
    Word* regs16_;

public:
    InterruptHandler(Dos *dos) : dos_(dos), regs8_(nullptr), regs16_(nullptr) {}
    void setup(Byte *regs8, Word *regs16) override { regs8_ = regs8; regs16_ = regs16; }
    IntStatus interrupt(const Byte num) override;

private:
    inline Byte& reg8(const int reg) { return regs8_[reg]; }
    inline Word& reg16(const int reg) { return regs16_[reg]; }
    inline const Byte& reg8(const int reg) const { return regs8_[reg]; }
    inline const Word& reg16(const int reg) const { return regs16_[reg]; }    
    inline Address csip() const;

    void dosFunction(const Byte funcHi, const Byte funcLo);
};

#endif // INTERRUPT_H