cmake_minimum_required(VERSION 3.5)

project(libdisasm LANGUAGES C)

set(LIBDISASM_HDR 
    libdisasm/ia32_implicit.h
    libdisasm/ia32_insn.h
    libdisasm/ia32_invariant.h
    libdisasm/ia32_modrm.h
    libdisasm/ia32_opcode_tables.h
    libdisasm/ia32_operand.h
    libdisasm/ia32_reg.h
    libdisasm/ia32_settings.h
    libdisasm/libdis.h
    libdisasm/qword.h
    libdisasm/x86_imm.h
    libdisasm/x86_operand_list.h)

set(LIBDISASM_SRC
    libdisasm/ia32_implicit.c
    libdisasm/ia32_invariant.c
    libdisasm/ia32_opcode_tables.c
    libdisasm/ia32_reg.c
    libdisasm/x86_disasm.c
    libdisasm/x86_imm.c
    libdisasm/x86_misc.c
    libdisasm/ia32_insn.c
    libdisasm/ia32_modrm.c
    libdisasm/ia32_operand.c
    libdisasm/ia32_settings.c
    libdisasm/x86_format.c
    libdisasm/x86_insn.c
    libdisasm/x86_operand_list.c)

add_library(libdisasm STATIC ${LIBDISASM_HDR} ${LIBDISASM_SRC})