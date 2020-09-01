/* Utility to test libdisasm. Disassembles from the start of a * file. */
/* Compile with  `gcc -I. -O3 -ggdb -L. -ldisasm testdis.c -o testdis` */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include "libdis.h"


int main(int argc, char *argv[])
{
	int f, i = 0, n, size;
	unsigned char *buf;
	void *image;
	x86_insn_t curr_inst;
	x86_invariant_t inv;
	struct stat sb;
	char line[80];

	if (argc < 2) {
		printf("Usage: %s filename\n", argv[0]);
		return 1;
	}
	
	f = open(argv[1], O_RDONLY);
	fstat(f, &sb);
	image = mmap(0, sb.st_size, PROT_READ, MAP_SHARED, f, 0);
	if (image == (void*)-1) {
		return (-1);
	}
	buf = (unsigned char *) image;
	close(f);
	printf("File name: %s\n", argv[1]);

	x86_init( opt_none, NULL, NULL );

	while (i < sb.st_size) {
		memset(&curr_inst, 0, sizeof (x86_insn_t));
		/* test invariant */
		size = x86_invariant_disasm( buf + i, sb.st_size - i, &inv );
		printf("%X\t", i);
		for ( n = 0; n < size; n++ ) {
			printf("%02X ", inv.bytes[n]);
		}
		printf("\t\t\t;invariant bytes (signature)\n");

		/* test code */
		printf("%X\t", i);
		size = x86_disasm( buf, sb.st_size, 0, i, &curr_inst );

		if (size) {
			for (n = 0; n < 12; n++) {
				if (n < size)
					printf("%02X ", buf[i + n]);
				else
					printf("   ");
			}

			x86_format_insn( &curr_inst, line, 80, att_syntax );
			printf( "%s\n", line );
			i += size;
		} else {
			printf("invalid opcode %02X\n", buf[i]);
			i++;
		}
	}

	munmap(image, sb.st_size);

	x86_cleanup();

	return 0;
}
