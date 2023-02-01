#include <algorithm>
#include <sstream>
#include "dos/registers.h"
#include "dos/util.h"

using namespace std;

const Word BYTE_HIGH = 8, BYTE_LOW = 0;
                                                   // AL         AH        BL         BH        CL         CH        DL         DH
static const Register PARENT_REG[]  = { REG_NONE,   REG_AX,    REG_AX,   REG_BX,    REG_BX,   REG_CX,    REG_CX,   REG_DX,    REG_DX };
static const Register SIBLING_REG[] = { REG_NONE,   REG_AH,    REG_AL,   REG_BH,    REG_BL,   REG_CH,    REG_CL,   REG_DH,    REG_DL };
static const Word BYTE_SHIFT[]      = {     0xff, BYTE_LOW, BYTE_HIGH, BYTE_LOW, BYTE_HIGH, BYTE_LOW, BYTE_HIGH, BYTE_LOW, BYTE_HIGH };

inline Word byteValue(const Word w, const Word s) {
    return (w & (0xff << s)) >> s;
}

Registers::Registers() {
    reset();
}

void Registers::reset() {
    fill(begin(values), end(values), 0);
    setFlag(FLAG_INT, true);
    // these are supposed to be always 1 on 8086
    setFlag(FLAG_B12, true); 
    setFlag(FLAG_B13, true);
    setFlag(FLAG_B14, true); 
    setFlag(FLAG_B15, true);    
}

Word Registers::get(const Register r) const {
    assert(r > REG_NONE && r <= REG_FLAGS);
    if (regIsWord(r)) return reg(r);
    assert(regIsByte(r));
    return byteValue(reg(PARENT_REG[r]), BYTE_SHIFT[r]);
}

void Registers::set(const Register r, const Word value) {
    assert(r > REG_NONE && r <= REG_FLAGS);
    if (regIsWord(r)) {
        reg(r) = value;
    }
    else {
        assert(value <= 0xff);
        Register parent = PARENT_REG[r], sibling = SIBLING_REG[r];
        reg(parent) = byteValue(value, BYTE_SHIFT[r]) | byteValue(reg(parent), BYTE_SHIFT[sibling]);
    }
}

string Registers::dump() const {
    ostringstream str;
    const Word flags = reg(REG_FLAGS);
    str << std::hex 
        << "IP = " << hexVal(reg(REG_IP)) << ", "
        << "FLAGS = " << hexVal(flags) << " / " << binString(flags) << " / "
        << "C" << ((flags & FLAG_CARRY) != 0) << " Z" << ((flags & FLAG_ZERO) != 0) << " S" << ((flags & FLAG_SIGN  ) != 0)
        << " O" << ((flags & FLAG_OVER ) != 0) << " A" << ((flags & FLAG_AUXC) != 0) << " P" << ((flags & FLAG_PARITY) != 0)
        << " D" << ((flags & FLAG_DIR  ) != 0) << " I" << ((flags & FLAG_INT ) != 0) << " T" << ((flags & FLAG_TRAP  ) != 0) << endl
        << "AX = " << hexVal(reg(REG_AX)) << ", BX = " << hexVal(reg(REG_BX)) << ", "
        << "CX = " << hexVal(reg(REG_CX)) << ", DX = " << hexVal(reg(REG_DX)) << endl
        << "SI = " << hexVal(reg(REG_SI)) << ", DI = " << hexVal(reg(REG_DI)) << ", "
        << "BP = " << hexVal(reg(REG_BP)) << ", SP = " << hexVal(reg(REG_SP)) << endl
        << "CS = " << hexVal(reg(REG_CS)) << ", DS = " << hexVal(reg(REG_DS)) << ", "
        << "SS = " << hexVal(reg(REG_SS)) << ", ES = " << hexVal(reg(REG_ES));
    return str.str();    
}
