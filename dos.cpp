#include "dos.h"

Dos::Dos(Cpu *cpu, Memory *memory) : cpu_(cpu), memory_(memory), freeMem_(0) {
}
