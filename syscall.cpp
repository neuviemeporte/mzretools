#include "syscall.h"
#include "memory.h"
#include "cpu.h"

Dos::Dos(Cpu *cpu, Arena *memory) : _cpu(cpu), _memory(memory), _freeMem(0) {
}
