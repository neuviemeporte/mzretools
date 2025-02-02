#include <algorithm>
#include <sstream>
#include <string>

#include "dos/registers.h"
#include "dos/util.h"
#include "dos/error.h"
#include "dos/output.h"

using namespace std;

const Word BYTE_HIGH = 8, BYTE_LOW = 0;
                                                   // AL         AH        BL         BH        CL         CH        DL         DH
static const Register PARENT_REG[]  = { REG_NONE,   REG_AX,    REG_AX,   REG_BX,    REG_BX,   REG_CX,    REG_CX,   REG_DX,    REG_DX };
static const Register SIBLING_REG[] = { REG_NONE,   REG_AH,    REG_AL,   REG_BH,    REG_BL,   REG_CH,    REG_CL,   REG_DH,    REG_DL };
static const Word BYTE_SHIFT[]      = {     0xff, BYTE_LOW, BYTE_HIGH, BYTE_LOW, BYTE_HIGH, BYTE_LOW, BYTE_HIGH, BYTE_LOW, BYTE_HIGH };

inline Word byteMask(const Word w, const Word s) {
    Word ret = w & (0xff << s);
    //debug("masking "s + hexVal(w) + " by " + to_string(s) + " = " + hexVal(ret));
    return ret;
}

inline Word byteValue(const Word w, const Word s) {
    return byteMask(w, s) >> s;
}

std::string regName(const Register r) {
    static const string regnames[] = {
        "NONE", 
        "AL", "AH", "BL", "BH", 
        "CL", "CH", "DL", "DH",
        "AX", "BX", "CX", "DX",
        "SI", "DI", "BP", "SP",
        "CS", "DS", "ES", "SS",
        "IP", "FLAGS"
    };
    assert(r >= REG_NONE && r <= REG_FLAGS);
    return regnames[r];
}

Register regHigh(const Register r) {
    static const Register high[] = { REG_AH, REG_BH, REG_CH, REG_DH };
    if (regIsGeneral(r)) return high[r - REG_AX];
    else return REG_NONE;
}

Register regLow(const Register r) {
    static const Register high[] = { REG_AL, REG_BL, REG_CL, REG_DL };
    if (regIsGeneral(r)) return high[r - REG_AX];
    else return REG_NONE;
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

Byte Registers::getHigh(const Register r) const {
    Register rh = regHigh(r);
    if (rh != REG_NONE) return static_cast<Byte>(reg(rh));
    else throw CpuError("Invalid register to get high byte: "s + regName(r));
}

Byte Registers::getLow(const Register r) const {
    Register rl = regLow(r);
    if (rl != REG_NONE) return static_cast<Byte>(reg(rl));
    else throw CpuError("Invalid register to get high byte: "s + regName(r));
}

void Registers::set(const Register r, const Word value) {
    assert(r >= REG_NONE && r <= REG_FLAGS);
    if (r == REG_NONE) 
        return;
    else if (regIsWord(r)) {
        reg(r) = value;
    }
    else {
        assert(value <= 0xff);
        Register parent = PARENT_REG[r], sibling = SIBLING_REG[r];
        Word w1 = value << BYTE_SHIFT[r];
        Word w2 = byteMask(reg(parent), BYTE_SHIFT[sibling]);
        reg(parent) = w1 | w2;
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

CpuState::CpuState() {
    for (int i = REG_AL; i <= REG_FLAGS; ++i) {
        Register r = (Register)i;
        regs_.set(r, 0);
        known_.set(r, 0);
    }
}

// TODO: set other known register types
CpuState::CpuState(const Address &code, const Address &stack) : CpuState() {
    setValue(REG_CS, code.segment);
    setValue(REG_IP, code.offset);
    // some test cases load code from binary files and cs:ip == ss:sp == 0:0 which leads to trouble
    if (stack != code) {
        setValue(REG_SS, stack.segment);
        setValue(REG_SP, stack.offset);
    }
}

bool CpuState::isKnown(const Register r) const {
    if (regIsWord(r)) return known_.get(r) == WORD_KNOWN;
    else return known_.get(r) == BYTE_KNOWN;
}

Word CpuState::getValue(const Register r) const {
    if (isKnown(r)) return regs_.get(r);
    else return 0;
}

void CpuState::setValue(const Register r, const Word value) {
    setState(r, value, true);
}

void CpuState::setUnknown(const Register r) {
    setState(r, 0, false);
}



string CpuState::stateString(const Register r) const {
    if (regIsWord(r))
        return (isKnown(r) ? hexVal(regs_.get(r), false, true) : "????");
    else
        return (isKnown(r) ? hexVal(static_cast<Byte>(regs_.get(r)), false, true) : "??");
}

string CpuState::regString(const Register r) const {
    string ret = regName(r) + " = ";
    if (regIsGeneral(r)) {
        if (isKnown(r)) ret += hexVal(regs_.get(r), false, true);
        else {
            ret += stateString(regHigh(r));
            ret += stateString(regLow(r));
        }
    }
    else ret += stateString(r);
    return ret;
}

string CpuState::toString() const {
    ostringstream str;
    str << std::hex 
        << regString(REG_AX) << ", " << regString(REG_BX) << ", "
        << regString(REG_CX) << ", " << regString(REG_DX) << endl
        << regString(REG_SI) << ", " << regString(REG_DI) << ", "
        << regString(REG_BP) << ", " << regString(REG_SP) << endl
        << regString(REG_CS) << ", " << regString(REG_DS) << ", "
        << regString(REG_SS) << ", " << regString(REG_ES) << endl
        << regString(REG_IP) << ", " << regString(REG_FLAGS) << endl;
    str << "stack: <";
    for (const Word &v : stack_) str << " " << hexVal(v, false);
    str << ">";
    return str.str();
}  

void CpuState::setState(const Register r, const Word value, const bool known) {
    if (regIsWord(r)) {
        regs_.set(r, value);
        known_.set(r, known ? WORD_KNOWN : 0);
    }
    else {
        regs_.set(r, value);
        known_.set(r, known ? BYTE_KNOWN : 0);
    }
}
