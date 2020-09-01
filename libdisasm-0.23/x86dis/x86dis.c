/* x86dis : command line disassembler using the libdisasm library */
/*
       x86dis [-a offset|--addr=offset]
              [-r offset len|--range=offset len]
              [-e offset|--entry=offset]
              [-s name|--syntax=name]
              [-f file|--file=file]
              [-o file|--out=file]
              [-l file|--log=file]
              [-h|-?|--help]
              [-v|--version]
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif


#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>

#include <libdis.h>


#define LIBDISASM_URL 		"http://bastard.sourceforge.net"
#define STDIN_PAGE_SIZE 	524288	/* 512 K */


enum dis_req_type { req_addr = 1, req_range, req_entry };
enum x86_options options = opt_none;

static struct DIS_REQ {
	unsigned long offset;
	unsigned int length;
	enum dis_req_type type;
	struct DIS_REQ *next;
} *dis_requests = NULL;

static struct DIS_INFO {
	/* file streams */
	FILE *in, *out, *err;
	/* size of input file */
	unsigned long size;
	/* flag for --entry option */
	int entry;
	/* pagesize for buffering STDIN */
	unsigned long pagesize;
	/* output syntax */
	enum x86_asm_format syntax;
} info = { NULL, NULL, NULL, 0, 0, STDIN_PAGE_SIZE, raw_syntax };


/* ------------------------------------------------------- REPORTER */
/* this is x86dis.c code that needs to be ripped off and used in x86dis.c */
void x86dis_reporter( enum x86_report_codes code, void *arg, void *junk ) {
	char * str;

	/* here we could examine the error and do something useful;
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
		default:	/* make GCC shut up */
			str = "Unknown Error";
			break;
	}

	fprintf(info.err, "X86DIS ERROR \'%s:\' 0x%08" PRIXPTR"\n", str, (unsigned long)arg);
}

/* ---------------------------------------------------------- PRINTER */
void x86dis_manual_print( x86_insn_t *insn, void *arg ) {
	char line[4096];
	int i;


	if ( x86_format_insn(insn, line, 4096, info.syntax) <= 0 ) {
		return;
	}

	if ( info.syntax == att_syntax || info.syntax == intel_syntax ) {
		/* print an address and hex bytes, since libdisasm doesn't */
		printf("%08" PRIX32, insn->addr );
		for ( i = 0; i < 10; i++ ) {
			if ( i < insn->size ) {
				printf(" %02X", insn->bytes[i]);
			} else {
				printf("   ");
			}
		}
		printf("\t");
	}

	printf("%s\n", line);
}


/* -------------------------------------------------------- RESOLVER */
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
			return(0);	/* already seen this rva */
		}
	}
	return(0);
}

/* In the resolver, we keep a list of RVAs we have seen and weed these out.
 * Needless to say, this is a simple example with poor performance. */

int32_t x86dis_resolver( x86_op_t *op, x86_insn_t *insn, void *arg ) {
	long retval = -1;

	if (! rva_list_add(insn->addr) ) {
		/* we have seen this one already; return -1 */
		return(-1);
	}

	/* this part is a flat ripoff of internal_resolver in libdis.c */
	/* we don't do any register or stack resolving */
	if ( op->type == op_absolute || op->type == op_offset ) {
		retval = op->data.sdword; /* no need to cast the void* */
	} else if (op->type == op_relative_near ){
		retval = insn->addr + insn->size + op->data.sbyte;
	} else if (op->type == op_relative_far ){
		if ( op->datatype == op_word ) {
			retval = insn->addr + insn->size + op->data.sword;
		} else if ( op->datatype == op_dword ) {
			retval = insn->addr + insn->size + op->data.sdword;
		}
	}

	return( retval );
}


/* -------------------------------------------------------- DISASM REQUESTS */
static int insert_request_after(struct DIS_REQ *req, struct DIS_REQ *curr ) {
	if (! curr ) {
		req->next = dis_requests;
		dis_requests = req;
	} else {
		req->next = curr->next;
		curr->next = req;
	}
	return(1);
}

static int add_request( enum dis_req_type type, unsigned long offset, 
		        unsigned int len ){
	struct DIS_REQ *request, *curr, *prev = NULL;

	if ( type == req_entry ) {
		info.entry = 1;
	}

	request = calloc( sizeof(struct DIS_REQ), 1 );
	if (! request ) {
		return(0);
	}

	request->type = type;
	request->offset = offset;
	request->length = len;

	if (! dis_requests ) {
		dis_requests = request;
		return(1);
	}

	curr = dis_requests;
	for ( curr = dis_requests; curr; prev = curr, curr = curr->next ) {
		/* put request in before current */
		if ( curr->offset > request->offset ) {
			insert_request_after( request, prev );
			break;
		}

		if ( curr->offset == offset ) {
			/* follow precedence of request types */
			if ( curr->type > request->type ) {
				insert_request_after( request, prev );
			} else {
				insert_request_after( request, curr );
			}
			break;
		}

		if ( ! curr->next ) {
			insert_request_after( request, curr );
			break;
		}

		/* else wait until one of the above conditions applies */
	}

	return(1);
}

static int do_request( enum dis_req_type type, unsigned char *buf, unsigned int 
		       buf_len, unsigned long buf_rva, unsigned long offset, 
		       unsigned int len ) {
	x86_insn_t insn;

	/* 'len' is optional, i.e. for a range param */
	switch (type) {
		case req_addr:
#ifdef DEBUG
			fprintf(info.err, "X86DIS: Disassemble address %lX\n", 
					offset );
#endif
			if ( offset > buf_len ) {
				fprintf(info.err, 
					"X86DIS: address %lX out of bounds\n", 
					offset );
				break;
			}
			if ( x86_disasm(buf, buf_len, buf_rva, offset, &insn) ){
				x86dis_manual_print( &insn, NULL );
			}
			break;

		case req_range:
#ifdef DEBUG
			fprintf(info.err, 
					"X86DIS: Disassemble %d bytes at %lX\n",
					len, offset );
#endif
			if ( len > buf_len ) {
				len = buf_len;
			}
			x86_disasm_range( buf, buf_rva, offset, len, 
				  x86dis_manual_print, NULL );
			break;
		case req_entry:
#ifdef DEBUG
			fprintf(info.err, 
				"X86DIS: Disassembly forward from %lX\n",
					offset );
#endif
			x86_disasm_forward( buf, buf_len, buf_rva, offset, 
				    x86dis_manual_print, NULL,
				    x86dis_resolver, NULL );
			break;
	}

	return( 1 );
}


/* -------------------------------------------------------- DISASM ACTIONS */
static int act_on_mmap( struct DIS_REQ *list, unsigned char *image, int len, 
		int base ){
	unsigned char *buf;
	struct DIS_REQ *req;

	/* cycle through requests, performing each on image */
	for ( req = list; req; req = req->next ) {
		buf = image;
		do_request( req->type, buf, len, base, req->offset, 
			    req->length ); 
	}
	return(1);
}

static int act_on_mmap_file( void ){
	unsigned char *image;
	struct stat sb;
	int fd = fileno(info.in);
	
	fstat(fd, &sb);

	/* create image from file */
	image = (unsigned char *) mmap( NULL, sb.st_size, PROT_READ, 
				        MAP_SHARED, fd, 0 );
	if ( image == (void*)-1 ) {
		fprintf( info.err, "Unable to map anonymous memory: %s\n",
				strerror(errno) );
		return(0);
	}

	return( act_on_mmap(dis_requests, image, sb.st_size, 0) );
}

static int mmap_stream( FILE *f, unsigned char **image ){
	int pos = 0, size = info.pagesize, cont = 1;

	/* create image from stream */
	*image = malloc( size );
	while ( cont ) {
		pos += fread(&((*image)[pos]), 1, info.pagesize, info.in);
		cont = !feof(info.in);
		if ( cont ) {
			size += info.pagesize;
			*image = realloc( *image, size );
		}
	}
	return( pos );
}

static int act_on_mmap_stream( void ){
	unsigned char *image;
	int len = mmap_stream(info.in, &image);
	return( act_on_mmap(dis_requests, image, len, 0) );
}

static int act_on_stream( void ){
	struct DIS_REQ *req;
	int size, pos = 0;
	unsigned char *bytes, buf[128];

	if ( info.entry ) {
		/* we need to have the whole stream in memory to do a -e */
		return( act_on_mmap_stream() );
	}

	for ( req = dis_requests; req; req = req->next ) {
		/* advance the stream until we reach request offset */
		while ( req->offset > pos ) {
			size = req->offset - pos;
			size = size > 128 ? 128 : size;
			/* advance the stream to request offset */
			fread( buf, size, 1, stdin );
			pos += size;
			if ( feof(stdin) ) {
				/* some kind of feedback here */
				break;
			}
		}

		if ( req->type == req_range && ! req->length ) {
			/* read to end of file ... via mmap ;) */
			size = mmap_stream( info.in, &bytes );
			act_on_mmap( req, bytes, size, pos );
			break;
		} else {
			if ( req->type == req_addr ) {
				size = x86_max_insn_size();
			} else {
				size = req->length;
			}
			if ( req->next && req->next->offset <= pos + size ) {
				/* crap ... overlapping requests
				 * mmap the thing and continue on from here */
				size = mmap_stream( info.in, &bytes );
				act_on_mmap( req, bytes, size, pos );
				break;
			}

			/* this calloc/free will need to be optimized
			 * if users do a lot of ops on STDIN ... hopefully
			 * they won't ;) */
			bytes = calloc( size, 1 );
			fread( bytes, size, 1, stdin );
			do_request( req->type, bytes, size, pos, req->offset, 
				    req->length ); 
		}
	}
	
	return(1);
}


/* -------------------------------------------------------- DISASM OPTIONS */
static enum x86_asm_format get_syntax_from_string( char *name ) {
	char *s, *d, lname[16] = {0};
	int i;
	
	for ( s = name, d = lname, i = 0; *s; s++, d++, i++ ) {
		
		if ( *s < 0x61 ) {
			*d = *s + 0x20;
		} else {
			*d = *s;
		}
		if ( *d < 0x61 || *d > 0x7A || i >= 15 ) { 
			/* bad input */
			fprintf( info.err, "Invalid syntax name: %s\n", name );
			return(0);
		}
	}

	if (! strcmp(lname, "att") ) {
		return( att_syntax );
	} else if (! strcmp(lname, "intel") ) {
		return( intel_syntax );
	} else if (! strcmp(lname, "raw") ) {
		return( raw_syntax );
	} else if (! strcmp(lname, "native") ) {
		return( native_syntax );
	} else if (! strcmp(lname, "xml") ) {
		return( xml_syntax );
	} else {
		fprintf( info.err, "Invalid syntax name: %s\n", lname );
	}

	return(unknown_syntax);
}

static int do_opt_s( char *name ) {
	enum x86_asm_format fmt;

	fmt = get_syntax_from_string( name );

	if ( fmt != unknown_syntax ) {
		info.syntax = fmt;
		if ( fmt == att_syntax ) {
			options |= opt_att_mnemonics;
		}
		else {
			options &= ~opt_att_mnemonics;
		}
	} else {
		return(0);
	}

	return(1);
}

static int do_opt_d( char *name ) {
	char buf[2048];
	enum x86_asm_format fmt;

	fmt = get_syntax_from_string( name );

	if ( fmt != unknown_syntax ) {
		if ( fmt == intel_syntax || fmt == att_syntax ) {
			/* we supply these, they are not in libdisasm syntax */
			fprintf(info.out,  "ADDRESS BYTES\t" );
		}
		x86_format_header( buf, 2046, fmt);
		fprintf(info.out, "%s\n", buf);
	} else {
		return(0);
	}

	return(1);
}

static int do_opt_f( char *name ) {
	struct stat sb;

	if (info.in != stdin ){
		fclose(info.in);
	}

	if ( stat(name, &sb) ) {
		info.in = stdin;
		fprintf( info.err, "Unable to open stat %s: %s\n",
				name, strerror(errno) );
		return(0);
	}

	info.in = fopen(name, "r");

	if ( !info.in ) {
		info.in = stdin;
		fprintf( info.err, "Unable to open file %s: %s\n",
				name, strerror(errno) );
		return(0);
	}
	return(1);
}

static int do_opt_o( char *name ) {
	if (info.out != stdout ){
		fclose(info.out);
	}

	info.out = fopen(name, "w+");

	if ( !info.out ) {
		info.out = stdout;
		fprintf( info.err, "Unable to open file %s: %s\n",
				name, strerror(errno) );
		return(0);
	}
	return(1);
}

static int do_opt_l( char *name ) {
	if (info.err != stderr ){
		fclose(info.err);
	}

	info.err = fopen(name, "w+");

	if ( !info.err ) {
		info.err = stderr;
		fprintf( info.err, "Unable to open file %s: %s\n",
				name, strerror(errno) );
		return(0);
	}
	return(1);
}

static void do_version(char *name) {
	printf("%s %s Distributed with libdisasm from %s\n", 
			name, PACKAGE_VERSION, LIBDISASM_URL);
}
static void do_help(char *name) {
	printf( "Usage: %s -aresfoldpLNhv\n"
		"Disassembles arbitrary bytes in a file or stream to x86 "
		"instructions.\n"
		"Options:\n"
		"\t-a offset     : disassemble instruction at offset\n"
		"\t-r offset len : disassemble range of bytes\n"
		"\t-e offset     : disassemble forward from offset\n"
		"\t-s name       : set output syntax"
				   "(intel, att, native, xml, raw)\n"
		"\t-f file       : take input from file\n"
		"\t-o file       : write output to file\n"
		"\t-l file       : write errors to file\n"
		"\t-d name       : display syntax description as header\n"
		"\t-p num        : memory map page size (default 512K)\n"
		"\t-L            : legacy (16-bit) mode\n"
		"\t-N            : no NULLs (ignore sequences of > 4 NULLs)\n"
		"\t-v            : display version information\n"
		"\t-h            : display this help screen\n"
		"\n"
		"The 'offset' and 'len' params must entered in stroul(3)format;"
		" any number or\n"
		"combination of -a, -r, and -e options may be used.\n"
		" Examples:  \n"
		"      x86dis -e 0 -s intel < bootsect.img\n"
		"      x86dis -d -s raw -f a.out -e `readelf -h a.out | "
		           "grep Entry | \n"
		"           awk '{ printf( \"0x%%x\", strtonum($4) - "
		           "0x8048000 ) }`\n"
		"      echo '55 89 e5 83 EC 08' | "
		           "perl -ane 'foreach(@F){print pack(\"C\",hex);}'|\n"
		"           x86dis -e 0 -s att\n",
	      name );
}

static int do_longarg( int argc, char **argv, int num ) {
	char *p, *arg1 = NULL, *arg2 = NULL, *opt = &argv[num][2];
	int n = num;
	unsigned long  off;
	unsigned int len;
	
	/* these take no parameters -- easy :) */
	if (! strcmp("help", opt) ) {
		do_help( argv[0] );
		return(0);
	} else if (! strcmp("version", opt) ) {
		do_version( argv[0] );
		return(0);
	}

	for ( p = opt; *p; p++ ) {
		if ( *p == '=' ) {
			arg1 = p;
		}
	}

	if ( ! arg1 ) { 
		n++;
		/* no '=' in argv[num] ... check argv[num++] */
		if ( n < argc ) {
			for ( p = argv[n]; *p; p++ ) {
				if ( *p == '=' ) {
					arg1 = p;
				}
			}
		}
	}

	if (! arg1 ) {
		return(-1);
	}

	/* arg1 and p now point to the '=' */
	for ( ; *p; p-- ) {
		/* next arg is part of this opt */
		if ( *p >= '0' && *p <= 'z' ) {
			arg1 = p;
		}
	}

	if (! *p ) {
		/* we didn't find the next argument */
		n++;
		arg1 = argv[n];
	}


	if (! strcmp("addr", opt) ) {
		/* --addr=offset */
		off = strtoul( arg1, NULL, 0 );
		add_request( req_addr, off, 0 );
	} else if (! strcmp("pagesize", opt) ) {
		/* --pagesize=num */
		off = strtoul( arg1, NULL, 0 );
		if (off) {
			info.pagesize = off;
		}
	} else if (! strcmp("range", opt) ) {
		/* --range=offset len */
		n++;
		if ( n < argc ) {
			arg2 = argv[n];
		} else {
			fprintf( info.err, "Missing range length param\n" );
			return(-1);
		}
		off = strtoul( arg1, NULL, 0 );
		len = (unsigned int) strtoul( arg2, NULL, 0 );
		add_request( req_range, off, len );
	} else if (! strcmp("entry", opt) ) {
		/* --entry=offset */
		off = strtoul( arg1, NULL, 0 );
		add_request( req_entry, off, 0 );
	} else if (! strcmp("syntax", opt) ) {
		/* --syntax=name */
		do_opt_s( arg1 );
	} else if (! strcmp("desc", opt) ) {
		/* --desc=name */
		do_opt_d( arg1 );
	} else if (! strcmp("file", opt) ) {
		/* --file=file */
		do_opt_f( arg1 );
	} else if (! strcmp("out", opt) ) {
		/* --out=file */
		do_opt_o( arg1 );
	} else if (! strcmp("log", opt) ) {
		/* --log=file */
		do_opt_l( arg1 );
	} else {
		return(0);
	}

	return(n - num);
}

int main( int argc, char **argv ) {
	char *name, c;
	int x, rv, error = 0;
	unsigned int len;
	unsigned long off;

	/* initialize default file streams */
	info.in = stdin;
	info.out = stdout;
	info.err = stderr;

	if ( argc < 2 ) {
		error = 1;
	}

	/* process arguments */
	for (x = 1; x < argc && ! error; x++) {
		c = argv[x][0];
		if (argv[x][0] == '-' ) {
			c = argv[x][1];
		}
		switch (c) {
			case '-':
			/* handle long arg */
				rv = do_longarg( argc, argv, x );
				if (rv < 0) {
					error = 1;
				}
				x+= rv;
				break;
			case 'a':
			/* -a offset : disasm single insn */
				x++;
				if ( x < argc ) {
					off = strtoul( argv[x], NULL, 0 );
					add_request( req_addr, off, 0 );
				} else {
					error = 1;
				}
				break;
			case 'r':
			/* -r offset len : disasm range */
				x+=2;
				if ( x < argc ) {
					off = strtoul( argv[x-1], NULL, 0 );
					len = (unsigned int) 
					      strtoul(argv[x], NULL, 0);
					add_request( req_range, off, 
						     len );
				} else {
					error = 1;
				}
				break;
			case 'e':
			/* -e offset : disasm forward from offset */
				x++;
				if ( x < argc ) {
					off = strtoul( argv[x], NULL, 0 );
					add_request(req_entry, off, 0);
				} else {
					error = 1;
				}
				break;
			case 's':
			/* -s name : set output syntax */
				x++;
				if ( x < argc ) {
					name = argv[x];
					do_opt_s( name );
				} else {
					error = 1;
				}
				break;
			case 'd':
			/* -d name : show syntax description */
				x++;
				if ( x < argc ) {
					name = argv[x];
					do_opt_d( name );
				} else {
					error = 1;
				}
				break;
			case 'f':
			/* -f file : read input from file */
				x++;
				if ( x < argc ) {
					name = argv[x];
					do_opt_f( name );
				} else {
					error = 1;
				}
				break;
			case 'o':
			/* -o file : write output to file */
				x++;
				if ( x < argc ) {
					name = argv[x];
					do_opt_o( name );
				} else {
					error = 1;
				}
				break;
			case 'l':
			/* -l file : write log/errors to file */
				x++;
				if ( x < argc ) {
					name = argv[x];
					do_opt_l( name );
				} else {
					error = 1;
				}
				break;
			case 'p':
			/* -p num : set pagesize */
				x++;
				if ( x < argc ) {
					off = strtoul( argv[x], NULL, 0 );
					if ( off ) {
						info.pagesize = off;
					}
				} else {
					error = 1;
				}
				break;
			case 'L':
			/* -L : use legacy 16-bit mode */
				options = options | opt_16_bit;
				break;
			case 'N':
			/* -N : use IGNORE NULLS mode */
				options = options | opt_ignore_nulls;
				break;
			case 'v':
			/* -v : version info */
				name = argv[0];
				do_version( name );
				break;
			case 'h':
			case '?':
			/* -h : help */
				name = argv[0];
				do_help( name );
				break;
			default:
				error = 1;
		}
	}

	if ( error ) {
		do_help(argv[0]);
		/* perform any cleanup */
		return(-1);
	}

	if (! dis_requests ) {
		/* -h or -v only */
		return(0);
	}

	/* initialize libdisasm */
	x86_init( options, x86dis_reporter, NULL);

	/* OK, do disassembly requests */
	if ( info.in != stdin ) {
		act_on_mmap_file();
	} else {
		act_on_stream();
	}

	/* shut down disassembler */
	x86_cleanup();

	return(0);
}


