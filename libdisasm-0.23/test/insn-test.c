#include <string.h>
#include <stdio.h>
#include <libdis.h>

int main() {
    x86_insn_t insn;
    int size;
    //unsigned char bytes[] = { 0xff, 0x25, 0xed, 0xc0, 0xad, 0xde };
    //unsigned char bytes[] = { 0x0f,0x0f,0xc8,0x1d };
    unsigned char bytes[] = { 0x0f,0x38 };
    char buf[1024];
  
    printf("init...\n");
    x86_init(opt_none, 0, 0);
  
    printf("disasm...\n");
    size = x86_disasm(bytes, sizeof(bytes), 0, 0, &insn);
    if (size != sizeof(bytes)) {
        printf("Eek: disasm %d bytes when given %d bytes\n", size, sizeof(bytes));
    }

    if (size) {
        if (insn.type == insn_invalid) {
            printf("Eek: got insn_invalid with size>0 (%d)!\n",size);
        }
        else {
            x86_format_insn(&insn, buf, 1024, att_syntax);
            printf("Got(%d): %s\n",size,buf);
        }
    }
    else {
        printf("Invalid instruction\n");
    }
  
    printf("free...\n");
    x86_oplist_free(&insn);
  
    printf("cleanup...\n");
    x86_cleanup();
  
    printf("done...\n");
    return 0;
}
