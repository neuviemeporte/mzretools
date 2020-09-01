/* A quick, dirty, stupid disassembler to test libdisasm */
/* Compile with  `gcc -I. -O3 -L. -ldisasm quikdis.c -o quikdis` */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <elf.h>
#include <unistd.h>
#include <inttypes.h>
#include "libdis.h"

void quikdis_reporter( enum x86_report_codes code, void *arg, void *junk ) {
        char * str = NULL;

        /* here would could examine the error and do something useful;
         * instead we just print that an error occurred */
        switch ( code ) {
                case report_disasm_bounds:
                        str = "Attempt to disassemble RVA beyond end of buffer";
                        break;
                case report_insn_bounds:
                        str = "Instruction at RVA extends beyond buffer";
                        break;
                case report_invalid_insn:
                        str = "Invalid opcode at RVA";
                        break;
                case report_unknown:
                        str = "Unknown Error";
                        break;
        }

        fprintf(stderr, "QUIKDIS: ERROR \'%s:\' %p\n", str, arg);
}

void quikdis_att_print( x86_insn_t *insn, void *arg ) {
        char line[256];
        x86_format_insn(insn, line, 256, att_syntax);
        printf( "%s\n", line);
}

void quikdis_native_print( x86_insn_t *insn, void *arg ) {
        char line[256];
        x86_format_insn(insn, line, 256, native_syntax);
        printf( "%s\n", line);
}

void quikdis_xml_print( x86_insn_t *insn, void *arg ) {
        char line[4096];
        x86_format_insn(insn, line, 4096, xml_syntax);
        printf( "%s\n", line);
}

void quikdis_raw_print( x86_insn_t *insn, void *arg ) {
        char line[1024];
        x86_format_insn(insn, line, 1024, raw_syntax);
        printf( "%s\n", line);
}

void quikdis_manual_print( x86_insn_t *insn, void *arg ) {
        char buf[MAX_OP_STRING];
	x86_op_t *op;
        int i;

        printf("%08" PRIX32, insn->addr );
        for ( i = 0; i < 10; i++ ) {
                if ( i < insn->size ) {
                        printf(" %02X", insn->bytes[i]);
                } else {
                        printf("   ");
                }
        }

        x86_format_mnemonic( insn, buf, MAX_OP_STRING, att_syntax );
        printf( "\t%s\t", buf );

	op = x86_operand_2nd( insn );
        if ( op && op->type != op_unused ) {
                x86_format_operand( op, buf, MAX_OP_STRING, att_syntax );
                /* if src is present, so is dest */
                printf("%s, ", buf);
        }

	op = x86_operand_1st( insn );
        if ( op && op->type != op_unused ) {
                x86_format_operand( op, buf, MAX_OP_STRING, att_syntax );
                printf("%s", buf);
        }

	op = x86_operand_3rd( insn );
        if ( op && op->type != op_unused ) {
                x86_format_operand( op, buf, MAX_OP_STRING, att_syntax );
                /* if src is present, so is dest */
                printf(", %s", buf);
        }
        printf("\n");
}

/* RESOLVER List support */
struct RVALIST {
        unsigned long rva;
        struct RVALIST *next;
} rva_list_head = {0};

static int rva_list_add( unsigned long rva ) {
        struct RVALIST *rl, *rl_new;

        for ( rl = &rva_list_head; rl; rl = rl->next ) {
                /* first rva is always 0 -- the list head */
                if ( rva > rl->rva ) {
                        if ( ! rl->next || rva < rl->next->rva ) {
                                /* we use exit() to free this, btw */
                                rl_new = calloc(sizeof(struct RVALIST), 1);
                                rl_new->rva = rva;
                                rl_new->next = rl->next;
                                rl->next = rl_new;
                                return(1);
                        }
                } else if ( rva == rl->rva ) {
                        return(0);      /* already seen this rva */
                }
        }
        return(0);
}

/* In the resolver, we keep a list of RVAs we have seen and weed these out.
 * Needless to say, this is a simple example with poor performance. */

int32_t quikdis_resolver( x86_op_t *op, x86_insn_t *insn, void *arg ) {
        long retval = -1;

        if (! rva_list_add(insn->addr) ) {
                /* we have seen this one already; return -1 */
                return(-1);
        }

        /* this part is a flat ripoff of internal_resolver in libdis.c */
        /* we don't do any register or stack resolving */
        if ( x86_optype_is_address(op->type) ) {
                retval = op->data.sdword; /* no need to cast the void* */
        } else if ( op->type == op_relative_near ) {
		retval = insn->addr + insn->size + op->data.relative_near;
        } else if ( op->type == op_relative_far ) {
		retval = insn->addr + insn->size + op->data.relative_far;
        }

        return( retval );
}

int main(int argc, char *argv[])
{
        void *image;
        int target_fd;
        struct stat sb;
        unsigned long buf_rva = 0;
        unsigned char *buf = NULL;
	char line[1024];
        unsigned int entry = 0, i, size, buf_len = 0;
        Elf32_Ehdr *elf_hdr;
        Elf32_Phdr *prog_hdr;
        x86_insn_t insn;        /* used for intel/loop disassembly */



        if (argc != 2) {
                printf("Usage: %s filename\n", argv[0]);
                return 1;
        }

        /* initialize libdisasm */
        x86_init(opt_none, quikdis_reporter, NULL);

        /* load target */
        target_fd = open(argv[1], O_RDONLY);
        fstat(target_fd, &sb);
        image = mmap(0, sb.st_size, PROT_READ, MAP_SHARED, target_fd, 0);
        if (image == (void*)-1)
                return (-1);
        close(target_fd);
        printf("Target File Name: %s\n", argv[1]);

        /* read ELF header */
        elf_hdr = image;

        /* iterate through program header table entries */
        for (i = 0; i < elf_hdr->e_phnum; i++) {
                prog_hdr = image + elf_hdr->e_phoff +
                           (i * elf_hdr->e_phentsize);

                /* IF entry point is in this section */
                if (elf_hdr->e_entry >= prog_hdr->p_vaddr &&
                    elf_hdr->e_entry <=
                    (prog_hdr->p_vaddr + prog_hdr->p_filesz)) {

                        /* resolve entry point RVA to a file offset */
                        entry = elf_hdr->e_entry -
                            (prog_hdr->p_vaddr - prog_hdr->p_offset);

                        /* use entire program segment as buffer */
                        buf = image + prog_hdr->p_offset;
                        buf_len = prog_hdr->p_filesz;
                        buf_rva = prog_hdr->p_vaddr;

                        break;  /* found what we need, now terminate */
                }
        }

        if ( buf ) {

                /* ------------------------------------------- */
                /* Disassembly using x86_disasm_range() */
#if 0
                printf("\n\n\n");
                printf("QUICKDIS Disassembly of .text: AT&T syntax\n");
                x86_disasm_range( buf, buf_rva, 0, buf_len,
                                  quikdis_att_print, NULL );
#endif


                /* ------------------------------------------- */
                /* Disassembly using x86_disasm in a loop */
                printf("\n\n\n");
                printf("QUICKDIS Disassembly of .text: Intel syntax\n");
                for ( i = 0; i < buf_len; ) {
                        size = x86_disasm( buf, buf_len, buf_rva, i, &insn );
                        if ( size ) {
                                x86_format_insn(&insn, line, sizeof line, 
						intel_syntax);
                                printf("%s\n", line);
                                i += size;
                        } else {
                                printf("invalid opcode %02X\n", buf[i]);
                                i++;
                        }
                }



                /* ------------------------------------------- */
                /* Disassembly using x86_disasm_forward */
                printf("\n\n\n");
                printf("QUICKDIS Disassembly following entry point\n");
                x86_disasm_forward( buf, buf_len, buf_rva, entry,
                                    quikdis_native_print, NULL,
                                    quikdis_resolver, NULL );


                /* ------------------------------------------- */
                /* Disassembly using x86_disasm_range and raw format */
                printf("\n\n\n");
                printf("QUICKDIS Disassembly of .text: RAW syntax\n");
                x86_disasm_range( buf, buf_rva, 0, buf_len,
                                  quikdis_raw_print, NULL );

                /* ------------------------------------------- */
                /* Disassembly using x86_disasm_range and xml format */
                printf("\n\n\n");
                printf("QUICKDIS Disassembly of .text: XML syntax\n");
                x86_disasm_range( buf, buf_rva, 0, buf_len,
                                  quikdis_xml_print, NULL );

                /* ------------------------------------------- */
                /* Disassembly using x86_disasm_range and manual formatting */
                printf("\n\n\n");
                printf("QUICKDIS Disassembly of .text: Manual AT&T syntax\n");
                x86_disasm_range( buf, buf_rva, 0, buf_len,
                                  quikdis_manual_print, NULL );
        }

        /* shut down disassembler */
        x86_cleanup();

        /* close everything */
        munmap(image, sb.st_size);
        return 0;
}
