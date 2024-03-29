#include <algorithm>
#include <iomanip>
#include "dos/psp.h"
#include "dos/util.h"

using namespace std;

ProgramSegmentPrefix::ProgramSegmentPrefix() :
    exit_instr(0x20cd),
    byte_past_segment(0),
    reserved_1(0),
    prev_term_addr(0),
    prev_break_addr(0),
    prev_err_addr(0),
    parent_psp_seg(0),
    env_seg(0),
    last_stack_addr(0),
    jft_size(0),
    jft_ptr(0),
    prev_psp(0),
    reserved_2(0),
    dos_ver(3),
    reserved_4(0),
    cmdline_size(0)
{
    fill(begin(dos_far_call1), end(dos_far_call1), 0);
    fill(begin(jft), end(jft), 0);
    fill(begin(reserved_3), end(reserved_3), 0);
    fill(begin(dos_far_call2), end(dos_far_call2), 0);
    fill(begin(reserved_5), end(reserved_5), 0);
    fill(begin(fcb_1), end(fcb_1), 0);
    fill(begin(fcb_2), end(fcb_2), 0);
    fill(begin(cmdline), end(cmdline), 0);
}

std::ostream& operator<<(std::ostream &os, const ProgramSegmentPrefix &arg) {
    return os << std::hex
        << "exit_instr = 0x" << hexString(arg.exit_instr) << endl
        << "byte_past_segment = 0x" << arg.byte_past_segment << endl
        << "reserved_1 = 0x" << static_cast<int>(arg.reserved_1) << endl
        << "dos_far_call1 = 0x" << hexString(arg.dos_far_call1) << endl
        << "prev_term_addr = 0x" << arg.prev_term_addr << endl
        << "prev_break_addr = 0x" << arg.prev_break_addr << endl
        << "prev_err_addr = 0x" << arg.prev_err_addr << endl
        << "parent_psp_seg = 0x" << arg.parent_psp_seg << endl
        << "jft = 0x" << hexString(arg.jft) << endl
        << "env_seg = 0x" << arg.env_seg << endl
        << "last_stack_addr = 0x" << arg.last_stack_addr << endl
        << "jft_size = 0x" << arg.jft_size << endl
        << "jft_ptr = 0x" << arg.jft_ptr << endl
        << "prev_psp = 0x" << arg.prev_psp << endl
        << "reserved_2 = 0x" << arg.reserved_2 << endl
        << "dos_ver = 0x" << arg.dos_ver << endl
        << "reserved_3 = 0x" << hexString(arg.reserved_3) << endl
        << "dos_far_call2 = 0x" << hexString(arg.dos_far_call2) << endl
        << "reserved_4 = 0x" << arg.reserved_4 << endl
        << "reserved_5 = 0x" << hexString(arg.reserved_5) << endl
        << "fcb_1 = 0x" << hexString(arg.fcb_1) << endl
        << "fcb_2 = 0x" << hexString(arg.fcb_2) << endl
        << "cmdline_size = 0x" << static_cast<int>(arg.cmdline_size) << endl
        << "cmdline = 0x" << hexString(arg.cmdline) << endl;
}