#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "dos/types.h"
#include "dos/memory.h"
#include "dos/registers.h"

enum IntStatus {
    INT_OK, INT_FAIL, INT_EXIT, INT_TERMINATE
};

class InterruptInterface {
public:
    virtual IntStatus interrupt(const Byte num, Registers &regs) = 0;
};

class Dos;

// an adapter class for dispatching interrupt requests from the CPU to other system components,
// whose aim is to decouple the CPU from these components. This class is a friend of the Cpu class
// so it can set the CPU registers according to the contract of the various interrupt functions,
// without the Cpu needing to be aware of the components, or the components being aware of 
// and/or poking at the Cpu.
class InterruptHandler : public InterruptInterface {
protected:
    Dos *dos_;

public:
    InterruptHandler(Dos *dos) : dos_(dos) {}
    IntStatus interrupt(const Byte num, Registers &regs) override;

private:
    void dosFunction(const Byte funcHi, const Byte funcLo, Registers &regs);
};

#endif // INTERRUPT_H