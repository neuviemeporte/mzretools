#include <algorithm>
#include <sstream>
#include "dos/registers.h"
#include "dos/util.h"

using namespace std;

Registers::Registers() {
    reset();
}

void Registers::reset() {
    fill(begin(regs_.bit16), end(regs_.bit16), 0);
    setFlag(FLAG_INT, true);
    // these are supposed to be always 1 on 8086
    setFlag(FLAG_B12, true); 
    setFlag(FLAG_B13, true);
    setFlag(FLAG_B14, true); 
    setFlag(FLAG_B15, true);    
}

string Registers::dump() const {
    ostringstream str;
    const Word flags = bit16(REG_FLAGS);
    str << std::hex 
        << "IP = " << hexVal(bit16(REG_IP)) << ", "
        << "FLAGS = " << hexVal(flags) << " / " << binString(flags) << " / "
        << "C" << ((flags & FLAG_CARRY) != 0) << " Z" << ((flags & FLAG_ZERO) != 0) << " S" << ((flags & FLAG_SIGN  ) != 0)
        << " O" << ((flags & FLAG_OVER ) != 0) << " A" << ((flags & FLAG_AUXC) != 0) << " P" << ((flags & FLAG_PARITY) != 0)
        << " D" << ((flags & FLAG_DIR  ) != 0) << " I" << ((flags & FLAG_INT ) != 0) << " T" << ((flags & FLAG_TRAP  ) != 0) << endl
        << "AX = " << hexVal(bit16(REG_AX)) << ", BX = " << hexVal(bit16(REG_BX)) << ", "
        << "CX = " << hexVal(bit16(REG_CX)) << ", DX = " << hexVal(bit16(REG_DX)) << endl
        << "SI = " << hexVal(bit16(REG_SI)) << ", DI = " << hexVal(bit16(REG_DI)) << ", "
        << "BP = " << hexVal(bit16(REG_BP)) << ", SP = " << hexVal(bit16(REG_SP)) << endl
        << "CS = " << hexVal(bit16(REG_CS)) << ", DS = " << hexVal(bit16(REG_DS)) << ", "
        << "SS = " << hexVal(bit16(REG_SS)) << ", ES = " << hexVal(bit16(REG_ES));
    return str.str();    
}