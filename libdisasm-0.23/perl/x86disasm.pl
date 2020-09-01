#!/usr/bin/perl

# settings for opcode.map file
my $opcode_dir=".";
my $opcode_file="$opcode_dir/i386.opcode.map";

# runtime temp vars
my $section;		# current section
my $i;			# iterator :P

# opcode tables
my @table_list;		# array of table_defs [numeric index in INSN struct]
my %tables;		# hash of table names: values = arrays of insn hashs
my %prefixes;		# hash or prefixes: values = prefix type

# command line options
my $opt_intcode;	# output intermediate format
my $opt_intel;		# output Intel syntax
my $opt_att;		# output AT&T syntax
my $opt_forward;	# disasm forward from entry
my $opt_text;		# disasm text sections
my $opt_phdr;		# use program headers
my $opt_shdr;		# use section headers
my $opt_xref;		# show cross-references
my $opt_quiet;		# suppress progress output
my $opt_entry;		# disassemble from forward $opt_entry
my $opt_section;	# disassemble $opt_section only
my $opt_hexbytes;	# number of hex bytes to print

my $target;		# what we are disassembling :)
my $target_image;		# buffer containing target
my $f;			# file stats

# disassembly info
my %target_info;

#  ===============================================================
#  Check options
if ( ! $ARGV[0] ) { 
	print_usage();
	exit(0);
}

while ( $ARGV[0] =~ /^-/ ) {
	if ( $ARGV[0] eq "-c" ) { $opt_intcode = 1; shift; }
	elsif ( $ARGV[0] eq "-i" ) { $opt_intel = 1; shift; }
	elsif ( $ARGV[0] eq "-a" ) { $opt_att = 1; shift; }
	elsif ( $ARGV[0] eq "-f" ) { $opt_forward = 1; shift; }
	elsif ( $ARGV[0] eq "-t" ) { $opt_text = 1; shift; }
	elsif ( $ARGV[0] eq "-p" ) { $opt_phdr = 1; shift; }
	elsif ( $ARGV[0] eq "-s" ) { $opt_shdr = 1; shift; }
	elsif ( $ARGV[0] eq "-x" ) { $opt_xref = 1; shift; }
	elsif ( $ARGV[0] eq "-q" ) { $opt_quiet = 1; shift; }
	elsif ( $ARGV[0] eq "-e" ) { shift;
		$opt_entry = shift;      $opt_forward = 1; }
	elsif ( $ARGV[0] eq "-S" ) { shift;
		$opt_section = shift; $opt_text = 1;    }
	elsif ( $ARGV[0] eq "-H" ) { shift; $opt_hexbytes = shift; }
	else  { print_usage(); exit(1); }
}

# set defaults
if ( !$opt_intcode && ! $opt_intel && ! $opt_att ) { $opt_att = 1; }
if ( !$opt_text && ! $opt_forward) { $opt_text = 1; }
if ( !$opt_shdr && ! $opt_phdr) { $opt_shdr = 1; }
if ( !$opt_hexbytes ) { $opt_hexbytes = 8; }

# load x86 opcode tables
$opt_quiet || print "Loading opcode tables from $opcode_file\n";
load_opcode_tables( $opcode_file );

#  ===============================================================
#  Open file for disassembly
$target = shift;
if ( $target ) {
	($f{dev},$f{ino},$f{mode},$f{nlink},$f{uid},$f{gid},$f{rdev},$f{size}, 
 	$f{atime},$f{mtime},$f{ctime},$f{blksize},$f{blocks}) = stat $target;
	$target_info{size} = $f{size};
} else {
	#$$target = "-";
	die "No target specified!\n";
}

open( TGT, $target ) || die "unable to open $target\n";
binmode( TGT, ":raw" );
$target_info{name} = $target;

#  ===============================================================
#  Parse file header to determine bytes to disassemble
$opt_quiet || print "Parsing ELF header\n";
$target_info{header} = elf_read();
if (! $target_info{header} ) { die "ERROR: $target is not an ELF file\n"};

#  ===============================================================
#  Do Disassembly

$opt_quiet || print "Performing Disassembly\n";

# read target into buffer
sysseek TGT, 0, SEEK_SET;
sysread TGT, $target_image, $f{size};

# disassemble, finally
if ( $opt_forward ) {
	if ( $opt_entry ) { 
		$target_info{entry} = hex($opt_entry);
		$target_info{entry_offset} = disasm_va2off($target_info{entry});
		if ( ! $target_info{entry} || ! $target_info{entry_offset} ) {
			die "ERROR: Invalid entry point '$opt_entry'\n";
		}
	}
	disasm_buffer( \$target_image, $target_info{entry_offset},
			$target_info{entry}, $f{size}, 1 );
} elsif ( $opt_text ) {
	if ( $opt_section ) {
		$section = $$target_info{sections}{$opt_section};
		if ( $$section{name} ) {
			disasm_section( $section, \$target_image );
		} else {
			die "ERROR: Invalid section '$opt_section'\n";
		}
	} else {
		
		foreach ( keys( %{$$target_info{sections}} ) ) {
			$section = $$target_info{sections}{$_};
			if ( $$section{type} eq "CODE" ) {
				disasm_section( $section, \$target_image );
			}
		}
	}
} else {
	die "Invalid disassembler option!\n";
}

# look for strings
$opt_quiet || print "Searching for strings in data sections\n";
disasm_strings();

# look for unrecognized subroutines
disasm_subroutines();

#  ===============================================================
#  Done... output the result
if ( $opt_intcode  ) {
	int_output();
} else {
	asm_output();
}

close( TGT );
exit;

#===============================================================================
# DEBUG routines
sub dbg_print_tables {
	local(%t);
	foreach (keys( %tables )) {
		print "TABLE $_:\n";
		# foreach instruution in table
		foreach ( @{$tables{$_}} ) {
			# print instruction
			%t = %$_;
			foreach (keys( %t )) {
				print "$_ = $t{$_},";
			}
			print "\n";
		}
	}
}

sub dbg_print_tablelist {
	local(%t);
	local($x) = 0;

	foreach ( @table_list ) {
		%t = %$_;
		printf"TABLE $x : ";
		foreach( keys(%t) ) {
			print "$_ = $t{$_},";
		}
		print "\n";
		$x++;
	}
}

#===============================================================================
# "NEW" routines
sub new_table {
	local($line) = shift;
	local(%t);

	$line =~ s/\s//g;
	($t{name}, $t{shft}, $t{mask}, $t{min}, $t{max}) = split ',', $line;
	return \%t;
}

sub new_insn {
	local($line) = shift;
	local(%i);

	$line =~ s/\s//g;
	$line =~ s/"//g;
	($i{table}, $i{iflg}, $i{dflg}, $i{sflg}, $i{aflg}, $i{cpu}, $i{insn}, 
	 $i{dest}, $i{src}, $i{aux}, $i{flags}) = split ',', $line;
	return \%i;
}

sub new_section {
	local($offset) = shift;
	local($size) = shift;
	local($va) = shift;
	local($name) = shift;
	local($type) = shift;
	local($perms) = shift;
	local(%s);

	$s{offset} = $offset;
	$s{size} = $size;
	$s{va} = $va;
	$s{type} = $type;
	$s{perms} = $perms;

	if ( defined($%{$target_info{sections}}{$name}) ) {
		$name .= "_$offset";		
	}
	$s{name} = $name;

	$$target_info{sections}{$name} = \%s;
	return \%s;
}

sub new_func {
	local($va) = shift;
	local($offset) = shift;
	local($name) = shift;;
	local(%func, $n, $sym);

	$sym = $$target_info{sym_idx}{$va};
	if ( $$sym{name} ) {
		$name = $$sym{name};
	}
	if ( ! $name ) {
		$n = new_name( $va, "sub_$va", "FUNCTION" );
		$name = $$n{name};
	}
	$func{name} = $name;
	$func{va} = $va;
	$func{offset} = $offset;
	push @{$target_info{functions}}, \%func;
	$$target_info{func_idx}{$va} = \%func;
	return \%func;
}

sub new_xref {
	local($from_va) = shift;
	local($to_va) = shift;
	local($type) = shift;
	local(%xref, $name);
	
	$name = sprintf "%08X_to_%08X", $from_va, $to_va;
	if ( $$target_info{xref_idx}{$name}{from} ) {
		return $$target_info{xref_idx}{$name};
	}

	$xref{name} = $name;
	$xref{from} = $from_va;
	$xref{to} = $to_va;
	$xref{type} = $type;

	push @{$target_info{xrefs}}, \%xref;
	$$target_info{xref_idx}{$name} = \%xref;
	return \%xref;
}

sub new_name {
	local($va) = shift;
	local($name) = shift;
	local($type) = shift;
	local(%n, $sym);

	# check for existing NAME
	if ( $$target_info{name_idx}{$va}{name} ) {
		return $$target_info{name_idx}{$va};
	}

	# check for existing symbol to use as name
	$sym = $$target_info{sym_idx}{$va};
	if ( $$sym{name} ) {
		$name = $$sym{name};
	}

	$n{va} = $va;
	$n{name} = $name;
	$n{type} = $type;

	push @{$target_info{names}}, \%n;
	$$target_info{name_idx}{$va} = \%n;

	return \%n;
}

sub new_string {
	local($va) = shift;
	local($offset) = shift;
	local($string) = shift;
	local(%s, $name);

	# check for existing STRING
	if ( $$target_info{string_idx}{$va}{string} ) {
		return $$target_info{string_idx}{$va};
	}

	# create name for STRING
	if ( ! $$target_info{name_idx}{$va}{name} ) {
		new_name( $va, "str_$va", "STRING" );
	}

	$s{va} = $va;
	$s{offset} = $offset;
	$s{string} = $string;

	push @{$target_info{strings}}, \%s;
	$$target_info{string_idx}{$va} = \%s;

	# add data item for STRING
	if ( $$target_info{data_idx}{$va}{size} ) {
		$$target_info{data_idx}{$va}{size} = length $string;
		$$target_info{data_idx}{$va}{type} = "STRING";
	} else {
		new_data( $va, $offset, length($string), "STRING" );
	}

	return \%s;
}

sub new_data {
	local($va) = shift;
	local($offset) = shift;
	local($size) = shift;
	local($type) = shift;
	local(%d, $sym, $n, $x, $hexstr, $buf);

	# check for existing NAME
	if ( $$target_info{data_idx}{$va}{size} ) {
		return $$target_info{data_idx}{$va};
	}
	
	# check for existing symbol to use as name
	$sym = $$target_info{sym_idx}{$va};
	if ( $$sym{name} ) {
		$d{name} = $$sym{name};
	#check for existing NAME
	} elsif ( $$target_info{name_idx}{$va}{name} ) {
		$d{name} = $$target_info{name_idx}{$va}{name};
	}

	if ( ! $d{name} ) {
		$n = new_name( $va, "var_$va", "DATA" );
		$d{name} = $$n{name};
	}

	$d{va} = $va;
	$d{offset} = $offset;
	$d{size} = $size;
	$d{type} = $type;

	# do hex bytes
	$hex_str = "";
	for ( $x = 0; $x < $size; $x ++ ) {
		$hex_str .= "C";
	}
	$buf = substr $target_image, $offset, $size;
	@{$d{bytes}} = unpack $hex_str, $buf;

	push @{$target_info{data}}, \%d;
	$$target_info{data_idx}{$va} = \%d;

	return \%d;
}

#===============================================================================
# OPCODE table management routines
sub table_from_index {
	local($idx) = shift;
	return $table_list[$idx];
}

sub insn_array_from_index {
	local($idx) = shift;
	local(%t);
	%t = %{ table_from_index($idx) };
	return $tables{$t{name}};
}

#  Prepare instruction tables from libdisasm opcode map
sub load_opcode_tables {
	local($file) = shift;
	local($intable, $tablelst, $prefixtbl);

	open(OPCODES, $file ) || die "Cannot open map $file\n";

	foreach ( <OPCODES> ) {
		if ( /^\s*instr\s+([A-Za-z0-9_]+)\[[0-9]*\]\s*=\s*\{/ ) {
			# we are in table named $1
			$intable = $1;
		} elsif ( /^\s*asmtable\s+tables86\[\]\s*=\s*\{/ ){
			# we are in the table of tables
			$tablelst = "tables86";
		} elsif ( /^\s*int\s+prefix_table\[13\]\[2\]\s*=\s*\{/ ){
			# we are in the table of prefixes
			$prefixtbl = "prefix_table";
		} elsif ( /^\s*\};/ ) {
			if ( $intable ) {
				# we are no longer in a table
				$intable = "";
			} elsif ( $tablelst ) {
				# we are no longer in the table of tables
				$tablelst = "";
			} elsif ( $prefixtbl ) {
				# we are no longer in the table of prefixes
				$prefixtbl = "";
			}
		} elsif ( /^\/\*[^*]+\*\/\s*\{\s*([^}]+)\s*\}/ ) {
			if ( $intable ) {
				# this must be an insn!
				$i = new_insn( $1 );
				push @{$tables{$intable}}, $i;
			}
		} elsif ( /^\s*\{\s*([^}]+)\s*\}/ ) {
			if ( $tablelst ) {
				# this must be a table
				push @table_list, new_table($1);
			} elsif ( $prefixtbl ) {
				# this is a prefix I guess
				if ($1 =~ /^\s*(0x[0-9A-F]+),(PREFIX_[A-Z_]+)/){
					$prefixes{$1} = $2;
				}
			} # else ignore
		}       # yup, ignore with extreme prejudice
	}

	# debug out if you want to convince yourself it works ;)
	#dbg_print_tables();
	#dbg_print_tablelist();
	close( OPCODES );

	return 1;
}

#===============================================================================
# ELF parsing routines

sub elf_read {
	local($buf);
	local(%e_hdr);
	local($elf_size) = 52;
	local($lil_endian) = "a16v2V5v6";
	local($big_endian) = "a16n2N5n6";
	local($id, $class, $data, $jnk);
	local($endian_str);


	sysread TGT, $buf, 16;
	($id, $class, $data, $jnk) = unpack "a4aaa10", $buf;
	
	if ( $id ne "\177ELF" ) {
		return 0;
	}

	# x86 is always little endian: this doubles as an ELF parsing lesson
	if ( $class == 2 ) {
		$target_info{bits} = 64;
	} else {
		$target_info{bits} = 32;	# default to 32 bits
	}
	if ( $data == 2 ) {
		$target_info{endian} = "big";
		$endian_str = $big_endian;
	} else {
		$target_info{endian} = "little";
		$endian_str = $lil_endian;
	}


	sysseek TGT, 0, SEEK_SET;
	sysread TGT, $buf, $elf_size;

	# We don't need all these values: once again, a Perl ELF lesson
	($e_hdr{e_ident}, $e_hdr{e_type}, $e_hdr{e_machine}, $e_hdr{e_version},
	 $e_hdr{e_entry}, $e_hdr{e_phoff}, $e_hdr{e_shoff}, $e_hdr{e_flags}, 
	 $e_hdr{e_ehsize}, $e_hdr{e_phentsize}, $e_hdr{e_phnum}, 
	 $e_hdr{e_shentsize}, $e_hdr{e_shnum}, $e_hdr{e_shstrndx} )  = 
		unpack $endian_str, $buf; 
		
	$target_info{header} = \%e_hdr;
	$target_info{entry} = $e_hdr{e_entry};

	$opt_quiet || print "\tHandling ELF Section headers\n";
	elf_shdr_read( \%e_hdr );
	$opt_quiet || print "\tHandling ELF Program headers\n";
	elf_phdr_read( \%e_hdr );
	$opt_quiet || print "\tGetting symbols from ELF dynamic linking info\n";
	elf_dynamic( \%e_hdr );

	return \%e_hdr;
}

sub elf_phdr_read {
	local($e_hdr) = shift;
	local($phdr);
	local($x);
 
	sysseek TGT, $$e_hdr{e_phoff}, SEEK_SET;
	for ( $x = 0; $x < $$e_hdr{e_phnum}; $x++ ) {
		# read header, create new header object
		sysread TGT, $buf, $$e_hdr{e_phentsize};
		$phdr = new_phdr( $buf );

		# add to list of program headers
		push @{$$e_hdr{phtab}}, $phdr;

 		next if (! $opt_phdr );		#section headers used for setup 

		# build target sections based on program headers
		if ( $$phdr{p_type} eq "PT_LOAD" ) {
			if ( $$phdr{p_flags} =~ /PF_X/ ) {
				new_section( $$phdr{p_offset}, $$phdr{p_filesz},
					$$phdr{p_vaddr}, ".text", "CODE" );
			} elsif ( $$phdr{p_flags} =~ /PF_W/ ) {
				new_section( $$phdr{p_offset}, $$phdr{p_filesz},
					$$phdr{p_vaddr}, ".data", "DATA" );
			} elsif ( $$phdr{p_flags} =~ /PF_R/ ) {
				new_section( $$phdr{p_offset}, $$phdr{p_filesz},
					$$phdr{p_vaddr}, ".rodata", "DATA" );
			}
		} elsif ( $$phdr{p_type} eq "PT_DYNAMIC" ) {
			$$e_hdr{dynhdr} = $phdr;
		} elsif ( $$phdr{p_type} eq "PT_INTERP" ) {
			$$e_hdr{interp} = $phdr;
		}
	}

	$target_info{entry_offset} = elf_va_to_offset( $e_hdr, 
							$target_info{entry} );
	return;
}

sub new_phdr {
	local($buf) = shift;
	local($phdr_str);
	local(%phdr);

	if ( $target_info{endian} eq "big" ) {
		$phdr_str = "NNNNNNNN";
	} else {
		$phdr_str = "VVVVVVVV";
	} 
	( $phdr{p_type}, $phdr{p_offset}, $phdr{p_vaddr}, $phdr{p_paddr}, 
	  $phdr{p_filesz}, $phdr{p_memsz}, $phdr{p_flags}, $phdr{p_align} ) =
	  	unpack $phdr_str, $buf;
	
	if ( $phdr{p_type} == 0 )      { $phdr{p_type} = "PT_NULL";
	} elsif ( $phdr{p_type} == 1 ) { $phdr{p_type} = "PT_LOAD";
	} elsif ( $phdr{p_type} == 2 ) { $phdr{p_type} = "PT_DYNAMIC";
	} elsif ( $phdr{p_type} == 3 ) { $phdr{p_type} = "PT_INTERP";
	} elsif ( $phdr{p_type} == 4 ) { $phdr{p_type} = "PT_NOTE";
	} elsif ( $phdr{p_type} == 5 ) { $phdr{p_type} = "PT_SHLIB";
	} elsif ( $phdr{p_type} == 6 ) { $phdr{p_type} = "PT_PHDR"; 
	} else {  $phdr{p_type} = "PT_UNK"; }

	$buf = "";
	if ( $phdr{p_flags} & 0x01 ) { $buf .= "PF_X|"; }
	if ( $phdr{p_flags} & 0x02 ) { $buf .= "PF_W|"; }
	if ( $phdr{p_flags} & 0x04 ) { $buf .= "PF_R|"; }
	$buf =~ s/\|$//g;
	$phdr{p_flags} = $buf;

	return \%phdr;
}

sub elf_shdr_read {
	local($e_hdr) = shift;
	local($shdr);
	local($x, $shstrtab, $shstrsz, $shstrbuf);

	if (! $$e_hdr{e_shoff} || ! $$e_hdr{e_shnum} ) {
		# this is an sstrip'ed binary: switch to phdr mode
		$opt_shdr = 0;
		$opt_phdr = 1;
		return;
	}

	sysseek TGT, $$e_hdr{e_shoff}, SEEK_SET;
	for ( $x = 0; $x < $$e_hdr{e_shnum}; $x++ ) {
		sysread TGT, $buf, $$e_hdr{e_shentsize};
		$shdr = new_shdr( $buf );

		if ( $x && $x == $$e_hdr{e_shstrndx} ) {
			$shstrtab = $$shdr{sh_offset};
			$shstrsz = $$shdr{sh_size};
		}
		# add to list of section headers 
		push @{$$e_hdr{shtab}}, $shdr;
	}

	if (! $opt_shdr || ! $shstrtab || ! $shstrsz )	 {
		$opt_phdr = 1;	# cannot do much w/o shstrtab
		return;		# prog headers used for setup
	}


	# read in copy of strtab
	sysseek TGT, $shstrtab, SEEK_SET;
	sysread TGT, $shstrbuf, $shstrsz;

	# Now that we know strtab, we can do real processing
	foreach ( @{$$e_hdr{shtab}} ) {
		$shdr = $_;
		$$shdr{sh_name} = substr $shstrbuf, $$shdr{sh_name};
		$$shdr{sh_name} =~ s/\x00.*//;

		if ( $$shdr{sh_name} eq ".dynamic" ) {
			# dynamic linking info
			$$e_hdr{dynhdr} = $shdr;
		} elsif ( $$shdr{sh_name} eq ".dynstr" ) {
			# dynamic linking strings
			$$e_hdr{dyn_strtab} = $$shdr{sh_offset};
			$$e_hdr{dyn_strsz} = $$shdr{sh_size};
		} elsif ( $$shdr{sh_name} eq ".dynsym" ) {
			# dynamic linking symbols
			$$e_hdr{dyn_symtab} = $$shdr{sh_offset};
			$$e_hdr{dyn_syment} = $$shdr{sh_entsize};
			$$e_hdr{dyn_symsz} = $$shdr{sh_size};
		} elsif ( $$shdr{sh_type} eq "SHT_PROGBITS" &&
		          $$shdr{sh_flags} =~ /SHF_ALLOC/ ) {
			if ( $$shdr{sh_flags} =~ /SHF_WRITE/ ) {
				# .data section 
				new_section( $$shdr{sh_offset}, $$shdr{sh_size},
			             $$shdr{sh_addr}, $$shdr{sh_name}, "DATA" );
			} elsif ( $$shdr{sh_flags} =~ /SHF_EXECINSTR/ ) {
				# .text
				new_section( $$shdr{sh_offset}, $$shdr{sh_size},
			             $$shdr{sh_addr}, $$shdr{sh_name}, "CODE" );
			} else {
				# .rodata
				new_section( $$shdr{sh_offset}, $$shdr{sh_size},
			           $$shdr{sh_addr}, $$shdr{sh_name}, "RODATA" );
			}
		} elsif ( $$shdr{sh_type} eq "SHT_NOGBITS" &&
		          $$shdr{sh_flags} =~ /SHF_ALLOC/ ) {
			if ( $$shdr{sh_flags} =~ /SHF_WRITE/ ) {
				# .bss section 
				new_section( $$shdr{sh_offset}, $$shdr{sh_size},
			             $$shdr{sh_addr}, $$shdr{sh_name}, "BSS" );
			}
		} # else ignore
	}
	
	return;
}

sub new_shdr {
	local($buf) = shift;
	local($shdr_str);
	local(%shdr);

	if ( $target_info{endian} eq "big" ) {
		$shdr_str = "NNNNNNNNNN";
	} else {
		$shdr_str = "VVVVVVVVVV";
	}
	
	( $shdr{sh_name}, $shdr{sh_type}, $shdr{sh_flags}, $shdr{sh_addr},
	  $shdr{sh_offset}, $shdr{sh_size}, $shdr{sh_link}, $shdr{sh_info},
	  $shdr{sh_addralign}, $shdr{sh_entsize} )  = unpack $shdr_str, $buf;

	if ( $shdr{sh_type} == 0 )      { $shdr{sh_type} = "SHT_NULL";
	} elsif ($shdr{sh_type} == 1 )  { $shdr{sh_type} = "SHT_PROGBITS";
	} elsif ($shdr{sh_type} == 2 )  { $shdr{sh_type} = "SHT_SYMTAB";
	} elsif ($shdr{sh_type} == 3 )  { $shdr{sh_type} = "SHT_STRTAB";
	} elsif ($shdr{sh_type} == 4 )  { $shdr{sh_type} = "SHT_RELA";
	} elsif ($shdr{sh_type} == 5 )  { $shdr{sh_type} = "SHT_HASH";
	} elsif ($shdr{sh_type} == 6 )  { $shdr{sh_type} = "SHT_DYNAMIC";
	} elsif ($shdr{sh_type} == 7 )  { $shdr{sh_type} = "SHT_NOTE";
	} elsif ($shdr{sh_type} == 8 )  { $shdr{sh_type} = "SHT_NOBITS";
	} elsif ($shdr{sh_type} == 9 )  { $shdr{sh_type} = "SHT_REL";
	} elsif ($shdr{sh_type} == 10 ) { $shdr{sh_type} = "SHT_SHLIB";
	} elsif ($shdr{sh_type} == 11 ) { $shdr{sh_type} = "SHT_DYNSYM"; }

	$buf = "";
	if ( $shdr{sh_flags} & 0x01 ) { $buf .= "SHF_WRITE|"; }
	if ( $shdr{sh_flags} & 0x02 ) { $buf .= "SHF_ALLOC|"; }
	if ( $shdr{sh_flags} & 0x04 ) { $buf .= "SHF_EXECINSTR|"; }
	$buf =~ s/\|$//g;
	$shdr{sh_flags} = $buf;

	return \%shdr;
}

sub elf_dynamic {
	local($e_hdr) = shift;
	local($dyntab) = $$e_hdr{dynhdr};
	local($dynsize) = 8;
	local($buf, $dyn_str, $str_buf, $x);
	local($sym);


	if ( $opt_phdr ) {
		# use program header data for dynamic section
		sysseek TGT, $$dyntab{p_offset}, SEEK_SET;
		for ( $x = 0; $x < $$dyntab{p_filesz}; $x += $dynsize ) {
			sysread TGT, $buf, $dynsize;
			$dyn = new_dyn( $buf );
			push @{$$e_hdr{dyntab}}, $dyn;
		}

		# identify strtab and symtab
		foreach ( @{$$e_hdr{dyntab}} ) {
			if ( $$_{d_tag} eq "DT_STRTAB" ) {
				$$e_hdr{dyn_strtab} = elf_va_to_offset( $e_hdr,
								$$_{d_val} );
			} elsif ( $$_{d_tag} eq "DT_SYMTAB" ) {
				$$e_hdr{dyn_symtab} = elf_va_to_offset( $e_hdr, 
								$$_{d_val} );
			} elsif ( $$_{d_tag} eq "DT_SYMENT" ) {
				$$e_hdr{dyn_syment} = $$_{d_val};
			} elsif ( $$_{d_tag} eq "DT_STRSZ" ) {
				$$e_hdr{dyn_strsz} = $$_{d_val};
			}
		}

		if (! $$e_hdr{dyn_strtab} || ! $$e_hdr{dyn_symtab} ) {
			return 0;
		}

		$$e_hdr{dyn_symsz} = $$e_hdr{dyn_strtab} - $$e_hdr{dyn_symtab};
	} # else rely on section headers 


	#read temporary copy of strtab
	sysseek TGT, $$e_hdr{dyn_strtab}, SEEK_SET;
	sysread TGT, $str_buf, $$e_hdr{dyn_strsz};

	# process symtab
	sysseek TGT, $$e_hdr{dyn_symtab}, SEEK_SET;
	for ( $x = 0; $x < $$e_hdr{dyn_symsz} / $$e_hdr{dyn_syment}; $x++ ){
		sysread TGT, $buf, $$e_hdr{dyn_syment};
		$sym = new_sym( $e_hdr, $buf, $str_buf );
		if ( $sym ) {
			push @{$target_info{symbols}}, $sym;
			# add to index
			$$target_info{sym_idx}{$$sym{va}} = $sym;
		}
	}

	return;
}

sub new_dyn {
	local($buf) = shift;
	local(%dyn);

	if ( $target_info{endian} eq "big" ) {
		$dyn_str = "NN";
	} else {
		$dyn_str = "VV";
	}
	( $dyn{d_tag}, $dyn{d_val} )  = unpack $dyn_str, $buf;
	
	if ( $dyn{d_tag} == 0 )       { $dyn{d_tag} = "DT_NULL";
	} elsif ( $dyn{d_tag} == 1 )  { $dyn{d_tag} = "DT_NEEDED";
	} elsif ( $dyn{d_tag} == 2 )  { $dyn{d_tag} = "DT_PLTRELSZ";
	} elsif ( $dyn{d_tag} == 3 )  { $dyn{d_tag} = "DT_PLTGOT";
	} elsif ( $dyn{d_tag} == 4 )  { $dyn{d_tag} = "DT_HASH";
	} elsif ( $dyn{d_tag} == 5 )  { $dyn{d_tag} = "DT_STRTAB";
	} elsif ( $dyn{d_tag} == 6 )  { $dyn{d_tag} = "DT_SYMTAB";
	} elsif ( $dyn{d_tag} == 7 )  { $dyn{d_tag} = "DT_RELA";
	} elsif ( $dyn{d_tag} == 8 )  { $dyn{d_tag} = "DT_RELASZ";
	} elsif ( $dyn{d_tag} == 9 )  { $dyn{d_tag} = "DT_RELAENT";
	} elsif ( $dyn{d_tag} == 10 ) { $dyn{d_tag} = "DT_STRSZ";
	} elsif ( $dyn{d_tag} == 11 ) { $dyn{d_tag} = "DT_SYMENT";
	} elsif ( $dyn{d_tag} == 12 ) { $dyn{d_tag} = "DT_INIT";
	} elsif ( $dyn{d_tag} == 13 ) { $dyn{d_tag} = "DT_FINI";
	} elsif ( $dyn{d_tag} == 14 ) { $dyn{d_tag} = "DT_SONAME";
	} elsif ( $dyn{d_tag} == 15 ) { $dyn{d_tag} = "DT_RPATH";
	} elsif ( $dyn{d_tag} == 16 ) { $dyn{d_tag} = "DT_SYMBOLIC";
	} elsif ( $dyn{d_tag} == 17 ) { $dyn{d_tag} = "DT_REL";
	} elsif ( $dyn{d_tag} == 18 ) { $dyn{d_tag} = "DT_RELSZ";
	} elsif ( $dyn{d_tag} == 19 ) { $dyn{d_tag} = "DT_RELENT";
	} elsif ( $dyn{d_tag} == 20 ) { $dyn{d_tag} = "DT_PLTREL";
	} elsif ( $dyn{d_tag} == 21 ) { $dyn{d_tag} = "DT_DEBUG";
	} elsif ( $dyn{d_tag} == 22 ) { $dyn{d_tag} = "DT_TEXTREL";
	} elsif ( $dyn{d_tag} == 23 ) { $dyn{d_tag} = "DT_JMPREL";
	} elsif ( $dyn{d_tag} == 24 ) { $dyn{d_tag} = "DT_BIND_NOW"; }

	return \%dyn;
}

sub new_sym {
	local($e_hdr) = shift;
	local($buf) = shift;
	local($strtab) = shift;
	local($sym_str);
	local(%sym, %symbol, $type, $bind);

	if ( $target_info{endian} eq "big" ) {
		$sym_str = "NNNCCn";
	} else {
		$sym_str = "VVVCCv";
	}

	( $sym{st_name}, $sym{st_value}, $sym{st_size}, $sym{st_info}, 
	  $sym{st_other}, $sym{st_shndx} ) =  unpack $sym_str, $buf;

	$bind = $sym{st_info} >> 4;
	$type = $sym{st_info} &0xF;
	
	$symbol{type} = "";
	if ( $bind == 0 ) {	
		$symbol{type} = "LOCAL|";
	} elsif ($bind == 1 ) {
		$symbol{type} = "GLOBAL|";
	}

	if ( $type == 1 ) {
		$symbol{type} .= "OBJECT";
	} elsif ( $type == 2 ) {
		 $symbol{type} .= "FUNCTION";
	} elsif ( $type == 3 ) {
		 $symbol{type} .= "FILE";
	} elsif ( $type == 4 ) {
		 $symbol{type} .= "SECTION";
	}
	$symbol{type} =~ s/^\|//g;
	$symbol{name} = substr $strtab, $sym{st_name};
	$symbol{name} =~ s/\x00.*//;
	$symbol{va} = $sym{st_value};
	$symbol{offset} =  elf_va_to_offset( $e_hdr, $sym{st_value} );

	return \%symbol;
}

sub elf_va_to_offset {
	local($e_hdr) = shift;
	local($va) = shift;
	local($phdr);

	foreach ( @{$$e_hdr{phtab}} ) {
		$phdr = $_;
		if ( $$phdr{p_vaddr} <= $va &&
		     ($$phdr{p_vaddr} + $$phdr{p_filesz}) > $va ) {
		     return( $$phdr{p_offset} + ($va - $$phdr{p_vaddr}) );
		}
	}
	return( $va );
}


#===============================================================================
# Register Data

# register table -- used for mapping opcode.map register IDs to reg names
# format: reg { name, type, size }
# types: REG_GENERAL, REG_SIMD, REG_DEBUG, REG_SYS, REG_CODESEG, REG_DATASEG,
#        REG_STACKSEG, REG_INVALID, REG_FPU, REG_CC, REG_FPU, REG_PC, REG_FP, 
#        REG_SP, REG_CNT, REG_RET, REG_SRC, REG_DEST
# Usage: disasm_get_reg( index, 0 ) to get reg by index
#        disasm_get_reg( 0, name ) to get reg by name
sub disasm_get_reg {
	local($num) = shift;
	local($name) = shift;
	local(@reg_table) = (
	{name => "eax", type => "REG_GENERAL,REG_RET", size => "OPSIZE_WORD"}, 
	{name => "ecx", type => "REG_GENERAL,REG_COUNT", size => "OPSIZE_WORD"}, 
	{name => "edx", type => "REG_GENERAL", size => "OPSIZE_WORD"}, 
	{name => "ebx", type => "REG_GENERAL", size => "OPSIZE_WORD"}, 
	{name => "esp", type => "REG_SP", size => "OPSIZE_WORD"}, 
	{name => "ebp", type => "REG_GENERAL,REG_FP", size => "OPSIZE_WORD"}, 
	{name => "esi", type => "REG_GENERAL,REG_SRC", size => "OPSIZE_WORD"}, 
	{name => "edi", type => "REG_GENERAL,REG_DEST", size => "OPSIZE_WORD"},
	{name => "ax", type => "REG_GENERAL,REG_RET", size => "OPSIZE_HWORD"}, 
	{name => "cx", type => "REG_GENERAL,REG_COUNT", size => "OPSIZE_HWORD"}, 
	{name => "dx", type => "REG_GENERAL", size => "OPSIZE_HWORD"}, 
	{name => "bx", type => "REG_GENERAL", size => "OPSIZE_HWORD"}, 
	{name => "sp", type => "REG_SP", size => "OPSIZE_HWORD"}, 
	{name => "bp", type => "REG_GENERAL,REG_FP", size => "OPSIZE_HWORD"}, 
	{name => "si", type => "REG_GENERAL,REG_SRC", size => "OPSIZE_HWORD"}, 
	{name => "di", type => "REG_GENERAL,REG_DEST", size => "OPSIZE_HWORD"},
	{name => "al", type => "REG_GENERAL", size => "OPSIZE_BYTE"}, 
	{name => "cl", type => "REG_GENERAL", size => "OPSIZE_BYTE"}, 
	{name => "dl", type => "REG_GENERAL", size => "OPSIZE_BYTE"}, 
	{name => "bl", type => "REG_GENERAL", size => "OPSIZE_BYTE"}, 
	{name => "ah", type => "REG_GENERAL", size => "OPSIZE_BYTE"}, 
	{name => "ch", type => "REG_GENERAL", size => "OPSIZE_BYTE"}, 
	{name => "dh", type => "REG_GENERAL", size => "OPSIZE_BYTE"}, 
	{name => "bh", type => "REG_GENERAL", size => "OPSIZE_BYTE"},
	{name => "mm0", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "mm1", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "mm2", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "mm3", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "mm4", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "mm5", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "mm6", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "mm7", type => "REG_SIMD", size => "OPSIZE_WORD"},
	{name => "xmm0", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "xmm1", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "xmm2", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "xmm3", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "xmm4", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "xmm5", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "xmm6", type => "REG_SIMD", size => "OPSIZE_WORD"}, 
	{name => "xmm7", type => "REG_SIMD", size => "OPSIZE_WORD"},
	{name => "dr0", type => "REG_DEBUG", size => "OPSIZE_WORD"}, 
	{name => "dr1", type => "REG_DEBUG", size => "OPSIZE_WORD"},
	{name => "dr2", type => "REG_DEBUG", size => "OPSIZE_WORD"}, 
	{name => "dr3", type => "REG_DEBUG", size => "OPSIZE_WORD"},
	{name => "dr4", type => "REG_DEBUG", size => "OPSIZE_WORD"}, 
	{name => "dr5", type => "REG_DEBUG", size => "OPSIZE_WORD"},
	{name => "dr6", type => "REG_DEBUG,REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "dr7", type => "REG_DEBUG,REG_SYS", size => "OPSIZE_WORD"},
	{name => "cr0", type => "REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "cr1", type => "REG_SYS", size => "OPSIZE_WORD"},
	{name => "cr2", type => "REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "cr3", type => "REG_SYS", size => "OPSIZE_WORD"},
	{name => "cr4", type => "REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "cr5", type => "REG_SYS", size => "OPSIZE_WORD"},
	{name => "cr6", type => "REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "cr7", type => "REG_SYS", size => "OPSIZE_WORD"},
	{name => "tr0", type => "REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "tr1", type => "REG_SYS", size => "OPSIZE_WORD"},
	{name => "tr2", type => "REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "tr3", type => "REG_SYS", size => "OPSIZE_WORD"},
	{name => "tr4", type => "REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "tr5", type => "REG_SYS", size => "OPSIZE_WORD"},
	{name => "tr6", type => "REG_SYS", size => "OPSIZE_WORD"}, 
	{name => "tr7", type => "REG_SYS", size => "OPSIZE_WORD"},
	{name => "es", type => "REG_DATASEG", size => "OPSIZE_HWORD"}, 
	{name => "cs", type => "REG_CODESEG", size => "OPSIZE_HWORD"},
	{name => "ss", type => "REG_STACKSEG", size => "OPSIZE_HWORD"}, 
	{name => "ds", type => "REG_DATASEG", size => "OPSIZE_HWORD"},
	{name => "fs", type => "REG_DATASEG", size => "OPSIZE_HWORD"}, 
	{name => "gs", type => "REG_DATASEG", size => "OPSIZE_HWORD"}, 
	{name => " ", type => "REG_INVALID", size => 0}, 
	{name => " ", type => "REG_INVALID", size => 0},
	{name => "st(0)", type => "REG_FPU", size => "OPSIZE_FPREG"}, 
	{name => "st(1)", type => "REG_FPU", size => "OPSIZE_FPREG"},
	{name => "st(2)", type => "REG_FPU", size => "OPSIZE_FPREG"}, 
	{name => "st(3)", type => "REG_FPU", size => "OPSIZE_FPREG"},
	{name => "st(4)", type => "REG_FPU", size => "OPSIZE_FPREG"}, 
	{name => "st(5)", type => "REG_FPU", size => "OPSIZE_FPREG"},
	{name => "st(6)", type => "REG_FPU", size => "OPSIZE_FPREG"}, 
	{name => "st(7)", type => "REG_FPU", size => "OPSIZE_FPREG"},
	{name => "eflags", type => "REG_CC", size => "OPSIZE_FPREG"}, 
	{name => "fpctrl", type => "REG_FPU,REG_SYS", size => "OPSIZE_HWORD"},
	{name => "fpstat", type => "REG_FPU,REG_SYS", size => "OPSIZE_HWORD"}, 
	{name => "fptag", type => "REG_FPU,REG_SYS", size => "OPSIZE_HWORD"},
	{name => "eip", type => "REG_PC", size => "OPSIZE_WORD"}, 
	{name => "ip", type => "REG_PC", size => "OPSIZE_HWORD"}
	);

	if ( $name ) {
		foreach( @reg_table ) {
			if ( $$_{name} eq $name ) {
				return $_;
			}
		}
		return 0;
	}
	return( $reg_table[$num] );
}

sub disasm_optable_regfix {
	local($op) = shift;
	local($reg_num);
	local(%reg_off) = (
		REG_DWORD_OFFSET 	=> 0,
		REG_WORD_OFFSET 	=> 8,
		REG_BYTE_OFFSET 	=> 16,
		REG_MMX_OFFSET 		=> 24,
		REG_SIMD_OFFSET 	=> 32,
		REG_DEBUG_OFFSET 	=> 40,
		REG_CTRL_OFFSET 	=> 48,
		REG_TEST_OFFSET 	=> 56,
		REG_SEG_OFFSET 		=> 64,
		REG_FPU_OFFSET 		=> 72,
		REG_FLAGS_INDEX 	=> 80,
		REG_FPCTRL_INDEX 	=> 81,
		REG_FPSTATUS_INDEX 	=> 82,
		REG_FPTAG_INDEX 	=> 83,
		REG_EIP_INDEX 		=> 84,
		REG_IP_INDEX 		=> 85
	);

	if ( $op =~ /([0-9]*)\s*\+?\s*(REG_[A-Z_]+)\s*\+?\s*([0-9]*)/ ) {
		$reg_num = $reg_off{$2} + $1 + $3;
		return $reg_num;
	} 
	return 0;
}

#===============================================================================
# Opcode disassembly routines
		

# disasm_get_imm( unsigned char *buf, int size, int sign );
# Returns operand
sub disasm_get_imm {
	local($buf) = shift;
	local($size) = shift;
	local($sign) = shift;
	local($imm_str) = "L";
	local($imm);
	
	if ( $sign ) {
		if ( $size == 1 ) { $imm_str = "c";
		} elsif ( $size == 2 ) { $imm_str = "s";
		} elsif ( $size == 4 ) { $imm_str = "i";
		} elsif ( $size == 8 ) { $imm_str = "q";
		} elsif ( $size == 16 ) { $imm_str = "q2";
		}
	} else {
		if ( $size == 1 ) { $imm_str = "C";
		} elsif ( $size == 2 ) { $imm_str = "v";
		} elsif ( $size == 4 ) { $imm_str = "V";
		} elsif ( $size == 8 ) { $imm_str = "Q";
		} elsif ( $size == 16 ) { $imm_str = "Q2";
		}
	}

	$imm = unpack $imm_str, $buf;

	return $imm;
}

sub byte_unpack {
	local($byte) = shift;
	local($a2, $b3, $c3);

	$c3 = $byte & 0x07;
	$b3 = ($byte >> 3) & 0x07;
	$a2 = ($byte >> 6) & 0x03;

	return ($a2, $b3, $c3);
}

# TYPE EADDR DISP|SCALE|INDEX|BASE
# Return size
sub disasm_modrm_decode {
	local($buf) = shift;
	local($op) = shift;		# pointer to insn{op}
	local($flg) = shift;		# pointer to insn{opflag}
	local($base_reg) = shift;
	local($ea)= shift;
	local($modrm, $mod, $reg, $rm);
	local($sib, $scale, $index, $base, $disp, $r, $eaddr);
	local($disp_flag, $base_flag, $idx_flag);
	local($count) = 1;
	
	($modrm, $sib) = unpack "CC", $buf;
	($mod, $reg, $rm) = byte_unpack $modrm;
	($scale, $index, $base) = byte_unpack $sib;

	if (! $ea ) { 		# this is using the 'reg' field of modR/M
		$r = disasm_get_reg($base_reg + $reg, 0);
		$$op = $$r{name};
		$$flg = "OP_REG,$$r{type},$$r{size}";
		return(0);		# no bytes consumed
	}
	if ( $mod  == 3 ) {		# mod = 11 [register, no memory addr]
		$r = disasm_get_reg($base_reg + $rm, 0);
		$$op = $$r{name};
		$$flg = "OP_REG,$$r{type},$$r{size}";
		return($count);
	}

	if ( ! $mod  ) {		# mod = 00 [no displacement]
		if ( $rm == 5 ) {	# rm = 101 [disp32 -- no reg]
			$disp = disasm_get_imm( substr($buf, $count), 4, 1 );
			$scale = $index = $base = "";
			$disp_flag = "DISP32";
			$count += 4;
		} elsif ( $rm == 4 ) {	# rm = 100 [no disp -- SIB]
			$count++;
			# DO SIB
			$scale = 0x01 << $scale;
			if ( $index != 4 ) {	# index = 100
				$r = disasm_get_reg($index, 0);
				$index = $$r{name};
				$idx_flag = "$$r{type},$$r{size}";
			} else {
				$index =  $scale = "";
			}

			if ( $base == 5 ) {	# base = 101, mod = 0
				$disp = disasm_get_imm( substr($buf, $count), 
									4, 1 );
				$disp_flag = "DISP32";
				$base = "";
			} else {
				$r = disasm_get_reg($base, 0);
				$base = $$r{name};
				$base_flag = "$$r{type},$$r{size}";
				$disp = "";
			}
		} else {		# register with no disp
			$r = disasm_get_reg($rm, 0);
			$base = $$r{name};
			$base_flag = "$$r{type},$$r{size}";
			$scale = $index = $disp = "";
		}
	} else {			# this is a disp[reg] combo
		# handle [SIB] or [register]
		if ( $rm == 4 ) {	# rm = 100 [disp8+SIB]
			$count++;
			# DO SIB
			$scale = 0x01 << $scale;
			if ( $index != 4 ) {	# index = 100
				$r = disasm_get_reg($index, 0);
				$index = $$r{name};
				$idx_flag = "$$r{type},$$r{size}";
			} else {
				$index =  $scale = "";
			}

			$r = disasm_get_reg($base, 0);
			$base = $$r{name};
			$base_flag = "$$r{type},$$r{size}";
		} else {		# disp[reg]
			$r = disasm_get_reg($rm, 0);
			$base = $$r{name};
			$base_flag = "$$r{type},$$r{size}";
			$scale = $index = "";
		}
		# handle displacement
		if ( $mod == 1 ) {	# mod = 01 [disp8]
			$disp = disasm_get_imm( substr($buf, $count), 1, 1 );
			$disp_flag = "DISP8";
			$count ++;
		} else {		# mod = 10 [disp32]
			$disp = disasm_get_imm( substr($buf, $count), 4, 1 );
			$disp_flag = "DISP32";
			$count += 4;
		}
		
	}

	if ( $scale == 1 ) { $scale = ""; }
	$$op = "$disp:$scale:$index:$base:$disp_flag:$idx_flag:$base_flag";
	$$flg = "OP_EADDR";
		
	return($count);
}

#disasm_operand_decode( INSN *insn, char *opname, unsigned char *buf );
#Examine operand $opname in $insn, using bytes in $buf for any encoded data.
#Return number of bytes "consumed" in $buf by decoding immediate values, etc.
sub disasm_operand_decode {
	local($insn) = shift;
	local($opname) = shift;
	local($main_buf) = shift;
	local($pos) = shift;
	local($sz_addr) = 4;		# 32-bit addresses
	local($sz_op) = 4;		# 32-bit operands
	local($addr_meth, $op_type, $base_reg, $reg);
	local($buf, $opflg, $opprm, $op, $flg, $size);
	
	if ( $$insn{type} =~ /PREFIX_ADDR_SIZE/ ) { $sz_addr = 2; }

	if ( $$insn{type} =~ /PREFIX_OP_SIZE/ ) { $sz_op = 2; }

	$op = $$insn{$opname};
	$$insn{$opname} = "";
	$opflg = substr( $opname, 0, 1 ) . "type";
	$flg = $$insn{$opflg};
	$$insn{$opflg} = "";		# clear flags field
	$opprm = substr( $opname, 0, 1 ) . "prm";
	
	# set operand permissions
	if ( $flg =~/\WOP_R(\W.*)*$/ ) { $$insn{$opprm} .="r"; }
	else {$$insn{$opprm} .="-";}
	if ( $flg =~/\WOP_W(\W.*)*$/ ) { $$insn{$opprm} .="w"; }
	else {$$insn{$opprm} .="-";}
	if ( $flg =~/\WOP_X(\W.*)*$/ ) { $$insn{$opprm} .="x"; }
	else {$$insn{$opprm} .="-";}

	# is operand hard-coded in insn?
	if ( $op ) {
		if ( $flg =~ /OP_REG/ ) {
			$reg = disasm_get_reg(disasm_optable_regfix($op), 0);
			$$insn{$opname} = $$reg{name};
			$$insn{$opflg} = "OP_REG,$$reg{type},$$reg{size}";
		} else {
			$$insn{$opname} = $op;
			$$insn{$opflg} = $flg;
			$$insn{$opflg} =~ s/\|/,/g;	# comma delim
		}
		return(0);	# no bytes used
	}

	if ( $flg =~ /ADDRMETH_([A-Z])/ ) { $addr_meth =$1; }
	if ( $flg =~ /OPTYPE_([a-z]+)/ ) { $op_type = $1; }

	# do operand size based on $op_type
	# My kingdom for a switch statement!
	if ( $op_type eq "c" ) {	# byte or hword by $sz_op
		if ( $sz_op == 4 ) {
			$sz_op = 2; $$insn{$opflg} .= ",OPSIZE_HWORD";
		} else {
			$sz_op = 1; $$insn{$opflg} .= ",OPSIZE_BYTE";
		}
	} elsif ($op_type eq "a" ) {	# 2 hwords or 2 words by $sz_op
		if ( $sz_op == 4 ) {
			$sz_op = 8; $$insn{$opflg} .= ",OPSIZE_DWORD";
		} else {
			$sz_op = 4; $$insn{$opflg} .= ",OPSIZE_WORD";
		}
	} elsif ($op_type eq "v" ) {	# hword or word by $sz_op
		if ( $sz_op == 4 ) {
			$$insn{$opflg} .= ",OPSIZE_WORD";
		} else {
			$$insn{$opflg} .= ",OPSIZE_HWORD";
		}
	} elsif ($op_type eq "p" ) {	# 32/48-bit ptr by $sz_op
		if ( $sz_op == 4 ) {
			$sz_op = 6; $$insn{$opflg} .= ",OPSIZE_6BYTE";
		} else {
			$sz_op = 4; $$insn{$opflg} .= ",OPSIZE_WORD";
		}
	} elsif ($op_type eq "b" ) {	# byte
		$sz_op = 1; $$insn{$opflg} .= ",OPSIZE_BYTE";
	} elsif ($op_type eq "w" ) {	# hword
		$sz_op = 2; $$insn{$opflg} .= ",OPSIZE_HWORD";
	} elsif ($op_type eq "d" ||	# word
	         $op_type eq "si" ) {	# dword integer register
		$sz_op = 4; $$insn{$opflg} .= ",OPSIZE_WORD";
	} elsif ($op_type eq "s" ) {	# 6-byte psuedo-descriptor
		$sz_op = 6; $$insn{$opflg} .= ",OPSIZE_6BYTE";
	} elsif ($op_type eq "q"  ||	# dword
	         $op_type eq "pi" ) {	# qword mmx register
		$sz_op = 8; $$insn{$opflg} .= ",OPSIZE_DWORD";
	} elsif ($op_type eq "m" ) {	# fake op type used for "lea Gv, M
		$sz_op = $sz_addr; 
		if ( $sz_addr == 4 ) { $$insn{$opflg} .= ",OPSIZE_WORD"; }
		else { $$insn{$opflg} .= ",OPSIZE_HWORD"; }
	} elsif ($op_type eq "dq" ) {	# quad word
		$sz_op = 16; $$insn{$opflg} .= ",OPSIZE_QWORD";
        } elsif ($op_type eq "ps" ) {	# 128-bit floating point
		$sz_op = 16; $$insn{$opflg} .= ",OPSIZE_FPDATA";
        } elsif ($op_type eq "ss" ) {	# 128 bit floating scalar
		$sz_op = 16; $$insn{$opflg} .= ",OPSIZE_FPSCALAR";
	} elsif ($op_type eq "fs" ) {	# single-real
		$sz_op = 4; $$insn{$opflg} .= ",OPSIZE_SREAL";
	} elsif ($op_type eq "fd" ) {	# double-real
		$sz_op = 4; $$insn{$opflg} .= ",OPSIZE_EREAL";
	} elsif ($op_type eq "fe" ) {	# extended real
		$sz_op = 4; $$insn{$opflg} .= ",OPSIZE_XREAL";
	} elsif ($op_type eq "fb" ) {	# packed BCD
		$sz_op = 4; $$insn{$opflg} .= ",OPSIZE_BCD";
	} elsif ($op_type eq "fv" ) {	# FPU env: 14/28-bytes
		$sz_op = 4; $$insn{$opflg} .= ",OPSIZE_FPENV";
	} else {
		if ( $sz_op == 4 ) {
			$$insn{$opflg} .= ",OPSIZE_WORD";
		} else {
			$$insn{$opflg} .= ",OPSIZE_HWORD";
		}
	}

	# override base index in register table
	if ( $sz_op == 1 ) { $base_reg = 16; }		# 1-byte reg
	elsif ($sz_op == 2) { $base_reg = 8; }		# 2-byte reg
	elsif ($sz_op == 8) { $base_reg = 24; }		# mmx reg

	# default return value and buffer position
	$size = 0;
	$buf = substr $main_buf, $pos;

	# handle operand based on addressing method
	if ($addr_meth eq "E") {	# modR/M EA, general reg or memory
		$size = disasm_modrm_decode( $buf, \$op, \$flg, $base_reg, 1 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
		$insn{modrm} = 1;
	} elsif ($addr_meth eq "M"){	# modR/M EA, memory only
		$size = disasm_modrm_decode( $buf, \$op, \$flg, 0, 1 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
		$insn{modrm} = 1;
	} elsif ($addr_meth eq "Q"){	# modR/M EA, mmx reg or memory
		$size = disasm_modrm_decode( $buf, \$op, \$flg, 24, 1 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
		$insn{modrm} = 1;
	} elsif ($addr_meth eq "R"){	# modR/M EA, general reg
		$size = disasm_modrm_decode( $buf, \$op, \$flg, $base_reg, 1 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
		$insn{modrm} = 1;
	} elsif ($addr_meth eq "W"){	# modR/M EA, SIMD reg or memory
		$size = disasm_modrm_decode( $buf, \$op, \$flg, 32, 1 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
		$insn{modrm} = 1;
	} elsif ($addr_meth eq "C"){	# modR/M reg: control reg
		$buf = $main_buf;
		disasm_modrm_decode( $buf, \$op, \$flg, 48, 0 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
	} elsif ($addr_meth eq "D"){	# modR/M reg: debug reg
		$buf = $main_buf;
		disasm_modrm_decode( $buf, \$op, \$flg, 40, 0 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
	} elsif ($addr_meth eq "G"){	# modR/M reg: general reg
		$buf = $main_buf;
		disasm_modrm_decode( $buf, \$op, \$flg, $base_reg, 0 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
	} elsif ($addr_meth eq "P"){	# modR/M reg: MMX reg
		$buf = $main_buf;
		disasm_modrm_decode( $buf, \$op, \$flg, 24, 0 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
	} elsif ($addr_meth eq "S"){	# modR/M reg: segment reg
		$buf = $main_buf;
		disasm_modrm_decode( $buf, \$op, \$flg, 64, 0 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
	} elsif ($addr_meth eq "T"){	# modR/M reg: test reg
		$buf = $main_buf;
		disasm_modrm_decode( $buf, \$op, \$flg, 56, 0 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
	} elsif ($addr_meth eq "V"){	# modR/M reg: SIMD reg
		$buf = $main_buf;
		disasm_modrm_decode( $buf, \$op, \$flg, 32, 0 );
		$$insn{$opflg} .= ",$flg"; $$insn{$opname} = $op;
	} elsif ($addr_meth eq "A"){	# direct address in insn
		$size = $sz_addr;
		$$insn{$opflg} .= ",OP_ADDR";
		$$insn{$opname} = disasm_get_imm( $buf, $sz_addr, 0 );
	} elsif ($addr_meth eq "F"){	# eflags register
		$$insn{$opflg} .= ",OP_REG,REG_CC";
		$$insn{$opname} = "eflags";
	} elsif ($addr_meth eq "I"){	# immediate value in insn
		$size = $sz_op;
		if ( $flg =~ /OP_SIGNED/ ) {
			$$insn{$opflg} .= ",OP_IMM,OP_SIGNED";
			$$insn{$opname} = disasm_get_imm( $buf, $sz_op, 1 );
		} else {
			$$insn{$opflg} .= ",OP_IMM";
			$$insn{$opname} = disasm_get_imm( $buf, $sz_op, 0 );
		}
	} elsif ($addr_meth eq "J"){	# immediate val = offset to eip
		$size = $sz_op;
		$$insn{$opflg} .= ",OP_REL,OP_SIGNED";
		$$insn{$opname} = disasm_get_imm( $buf, $sz_op, 1 );
	} elsif ($addr_meth eq "O"){	# offset (va) in insn
		$size = $sz_op;
		$$insn{$opflg} .= ",OP_OFF,OP_SIGNED";
		$$insn{$opname} = disasm_get_imm( $buf, $sz_op, 1 );
	} elsif ($addr_meth eq "X"){	# memory addresses by DS:ESI
		$$insn{$opflg} .= ",OP_REG,OP_STRING,REG_GENERAL,REG_SRC";
		$$insn{$opname} = "ds:esi";
	} elsif ($addr_meth eq "Y"){	# memory addresses by ES:EDI
		$$insn{$opflg} .= ",OP_REG,OP_STRING,REG_GENERAL,REG_DEST";
		$$insn{$opname} = "es:edi";
	}

	$$insn{$opflg} =~ s/^,//;
	return $size;
}
	
# disasm_table_adjust_byte( OPCODE_TABLE_DEF *tbl_def, unsigned char *byte );
# Adjust $byte to ranges of table, return $byte adjusted to be index into table
sub disasm_table_adjust_byte {
	local($tbl_def) = shift;
	local($byte) = shift;

	# used for tables < 256 values 
	if ( (hex($$tbl_def{max}) < 0xFF) && $byte > hex($$tbl_def{max}) ) {
		$tbl_def = $table_list[$table_num + 1];
	}

	# used for tables < 256 values 
	if ( hex($$tbl_def{min}) ) {
		$byte -= hex($$tbl_def{min});
	}

	# overcome perl's & and >> stupidity
	$$tbl_def{shft} *= 1;

	# used for tables < 256 values 
	if ( $$tbl_def{shft} ) {
		$byte = $byte >> $$tbl_def{shft};
	}
	# this is a modr/m extension
	$byte &= hex($$tbl_def{mask});

	return( $byte );
}

#disasm_table_lookup( int table_num, unsigned char *buf, INSN *insn);
#Use bytes in $buf to look up instruction in opcode table # $table_num
#Fill $insn with instruction details. Return size [# of bytes used to decode].
sub disasm_table_lookup {
	local($table_num) = shift;
	local($buf) = shift;
	local($insn) = shift;
	local($size) = 1;
	local($tbl_def, $insn_def, $table, $byte, $prefix);

	$byte = unpack "C", $buf;
	$tbl_def = $table_list[$table_num];

	$byte = disasm_table_adjust_byte( $tbl_def, $byte );

	# adjust buf unless this opcode was from a modrm nyte
	if ( hex($$tbl_def{mask}) != 0xFF ) {
		$size = 0;
	}

	$table = $tables{$$tbl_def{name}};
	$insn_def = $$table[$byte];

	if (! $insn_def ) {
		return 0;
	} elsif ( $$insn_def{table} ) {
		return ( $size + disasm_table_lookup($$insn_def{table}, 
						substr($buf, 1), $insn)  );
	} elsif ( $$insn_def{iflg} =~ /INSTR_PREFIX/ ) {
		# get prefix, save in insn
		$prefix = sprintf "0x%02X", $byte;

		if ( $prefixes{$prefix} eq "PREFIX_LOCK" ) {
			$$insn{type} .= ",INS_LOCK";
		} elsif ( $prefixes{$prefix} eq "PREFIX_REPNZ" ) {
			$$insn{type} .= ",INS_REPNZ";
		} elsif ( $prefixes{$prefix} eq "PREFIX_REPZ" ) {
			$$insn{type} .= ",INS_REPZ";
		} else {
			$$insn{type} .= ",$prefixes{$prefix}";
			$$insn{type} =~ s/PREFIX_/SEG_/;
		}
		$$insn{type} =~ s/^,//;

		return ( 1 + disasm_table_lookup(0, substr($buf, 1), 
							$insn) );
	} else {
		$buf = substr $buf, $size;

		# fill mnemonics
		$$insn{mnemonic} = $$insn_def{insn};
		# "type" may already contain a prefix
		$$insn{type} .= "$$insn_def{iflg},";	# insn type
		$$insn{type} .= "$$insn_def{cpu}";	# cpu req.
		$$insn{type} =~ s/,cpu_[A-Za-z0-9]*//;
		$$insn{flags} = $$insn_def{flags};	# flags effected

		# fill insn operands
		$$insn{dest} = $$insn_def{dest};	# destination
		$$insn{dtype} = $$insn_def{dflg};
		$$insn{src} = $$insn_def{src};		# source
		$$insn{stype} = $$insn_def{sflg};
		$$insn{aux} = $$insn_def{aux};		# third op (imm)
		$$insn{atype} = $$insn_def{aflg};

	}
	return $size;

}

#===============================================================================
# Disassembly "helper" routines

sub section_strings {
	local($sec) = shift;
	local($buf, $pos, $num);

	$pos = 0;
	$num = 0;
	$buf = substr $target_image, $$sec{offset}, $$sec{size};
	while ($pos < $$sec{size}) {
		for ($x = 0; $x + $pos < $$sec{size}; $x ++ ) {
			$c = chr(unpack "c", substr($buf,$x,1));
			if ( $c !~ /[ \n!"#\$%&'()*+,-.\/0-z{}|~\s]/ ) {
				last;
			}
		}
		if ($x >= 4 ) {
			$str = substr($buf, 0, $x);
			$pos += $x;
			$num++;
				new_string( $$sec{va} + $pos,
					    $$sec{offset} + $pos,
					    $str );
			$buf = substr $buf, $x;
		} else {
			$pos++;	
			$buf = substr $buf, 1;
		}
	}
	return $num;
}
	
sub disasm_strings {
	local($sec, $num);

	foreach ( keys( %{$$target_info{sections}} ) ) {
		$sec = $$target_info{sections}{$_};
		if ( $$sec{type} !~ "CODE" ) {
			$num = section_strings( $sec );
			$opt_quiet || print "\t$$sec{name} : $num found\n";
		}
	}
	return;
}

sub disasm_subroutines {
	local($insn, $next);
	foreach ( sort( keys( %{$$target_info{insn_idx}} ) ) ) {
		$insn = $$target_info{insn_idx}{$_};
		# look for 'push ebp'
		if ( $$insn{mnemonic} =~ /^push/ && $$insn{dest} =~ /bp$/ ) {
			$next = $$target_info{insn_idx}{ 
						$$insn{va} + $$insn{size} };
			# look for 'mov ebp, esp'
			if ( $$next{mnemonic} =~ /^mov/ && 
			     $$next{dest} =~ /bp$/  &&
			     $$next{src} =~ /sp$/ ) {
					new_func($$insn{va}, $insn{offset}, 0);

			}
		}
	}
	return;
}

sub xrefs_to {
	local($to_va) = shift;
	local(@xrefs);

	#foreach ( keys( %{$$target_info{xref_idx}} ) ) {
	foreach ( @{$target_info{xrefs}} ) {
		if ( $$_{to} == $to_va ) {
			push @xrefs, $_;
		}
	}
	return @xrefs;
}

#disasm_branch_target($$insn{dest}, $$insn{dtype});
sub disasm_branch_target {
	local($insn) = shift;
	local($op) = shift;
	local($type) = shift;

	# try to make an OP_ADDR out of the operand
	if ( $type =~ /OP_OFF/) {
		if ( $type =~ /OPSIZE_BYTE/ ) {
			return $$insn{va} + $$insn{size} + $op;
		}
		return $op;
	}

	if ( $type =~ /OP_REL/) {
		return $$insn{va} + $$insn{size} + $op;
	}

	if ( $type =~ /OP_IMM/ ) {
		if ( $type =~ /WORD/ && $type !~ /OP_SIGNED/ ) {
			return $op;
		}
	}

	if ( $type =~ /OP_ADDR/ ) {
		return $op;
	} 
	return -1;
}

sub disasm_va2off {
	local($va) = shift;
	local($sec);

	foreach ( keys( %{$$target_info{sections}} ) ) {
		$sec = $$target_info{sections}{$_};
		if ($va >= $$sec{va} && $va < $$sec{va} + $$sec{size} ) {
			return $$sec{offset} + ($va - $$sec{va});
		}
	}
	return(-1);
}

sub disasm_is_addr {
	local($va) = shift;
	foreach ( keys( %{$$target_info{sections}} ) ) {
		$sec = $$target_info{sections}{$_};
		if ( $va >= $$sec{va} &&
		     $va < $$sec{va} + $$sec{size} ) {
			return 1;
		}
	}
	return 0;
}

sub disasm_is_data_addr {
	local($va) = shift;
	foreach ( keys( %{$$target_info{sections}} ) ) {
		$sec = $$target_info{sections}{$_};
		if ( $$sec{type} =~ /DATA/ &&
		     $va >= $$sec{va} &&
		     $va < $$sec{va} + $$sec{size} ) {
			return 1;
		}
	}
	return 0;
}

sub disasm_is_code_addr {
	local($va) = shift;
	foreach ( keys( %{$$target_info{sections}} ) ) {
		$sec = $$target_info{sections}{$_};
		if ( $$sec{type} =~ /CODE/ &&
		     $va >= $$sec{va} &&
		     $va < $$sec{va} + $$sec{size} ) {
			return 1;
		}
	}
	return 0;
}

sub disasm_op_size {
	local($type) = shift;

	if ( $type =~ /OPSIZE_QWORD/ )	{ return 16; }
	if ( $type =~ /OPSIZE_DWORD/ )	{ return 8; }
	if ( $type =~ /OPSIZE_HWORD/ )	{ return 2; }
	if ( $type =~ /OPSIZE_BYTE/ )	{ return 1; }
	if ( $type =~ /OPSIZE_WORD/ )	{ return 4; }
	if ( $type =~ /OPSIZE_6BYTE/ )	{ return 6; }
	if ( $type =~ /OPSIZE_FPDATA/ )	{ return 16; }
	if ( $type =~ /OPSIZE_FPSCALAR/ )	{ return 16; }
	if ( $type =~ /OPSIZE_FPENV/ )	{ return 4; }
	if ( $type =~ /OPSIZE_SREAL/ )	{ return 4; }
	if ( $type =~ /OPSIZE_EREAL/ )	{ return 4; }
	if ( $type =~ /OPSIZE_XREAL/ )	{ return 4; }
	if ( $type =~ /OPSIZE_BCD/ )	{ return 4; }

	return 4;	# default size
}

sub disasm_do_op {
	local($va) = shift;
	local($op) = shift;
	local($type) = shift;
	local($perm) = shift;
	local($size);	
	local($disp,$scale,$index,$base,$flags);

	if ( $type =~ /OP_EADDR/ ) {
		($disp,$scale,$index,$base,$flags) = split /:/, $op;
		#see if we can do anything with the disp
		if ( ! $scale && ! $index && ! $base && $flags=~/DISP32/ ) {
			$op = $disp;
			$type = "OP_ADDR";
		}
	}
	if ( $type =~ /OP_IMM/ && disasm_is_addr($op) ) {
		$type = "OP_ADDR";
	}
	
	if ( $type =~ /OP_ADDR/ || 
	     ($type =~ /OP_OFF/ &&  $type !~ /OPSIZE_BYTE/) ) {
		if ( $perm =~ /r/ ) {
			new_xref( $va, $op, "r" );
		}
		if ( $perm =~ /w/ ) {
			new_xref( $va, $op, "w" );
		}
		if ( ! disasm_is_data_addr($va) ) {
			$size = disasm_op_size($type);
			new_data( $va, disasm_va2off($va), $size, 0);
		}
	}

	return;
}

sub disasm_check_insn {
	local($insn) = shift;
	local($sym, $name);

	# check for a name for this address
	$sym = $$target_info{sym_idx}{$$insn{$va}};
	$name = $$target_info{name_idx}{$$insn{$va}};

	if ( $$sym{name} ) {
		$$insn{name} = $$sym{name};
	} elsif ( $$name{name} ) {
		$$insn{name} = $$name{name};
	}

	# check for addresses in operands
	# branches are handled in disassemble_buffer()
	if ( $$insn{type} =~ /(BRANCH)|(CALL)|(INS_RET)/ ) { return; }

	if ( $$insn{stype} ) { 
		disasm_do_op( $va, $$insn{src}, $$insn{stype}, $$insn{sprm} );
	}
	if ( $$insn{dtype} ) { 
		disasm_do_op( $va, $$insn{dest}, $$insn{dtype}, $$insn{dprm} );
	}
	if ( $$insn{atype} ) { 
		disasm_do_op( $va, $$insn{aux}, $$insn{atype}, $$insn{aprm} );
	}

	# additional stuff like stack management can go here
	return;
}

# disasm_addr( char *buf, int max );
# Call disasm_table_lookup to get insn based on up to $max bytes in $buf
# Fix operands, fill INSN %i with instruction details. Return %i.
sub disasm_addr {
	local($buf) = shift;
	local($max) = shift;
	local($size, $bytes);
	local(%i);

	$size = disasm_table_lookup( 0, $buf, \%i );

	if (! $size ) {	return(0); }
	# advance buffer "pointer"
	$buf = substr $buf, $size;

	# decode operands
	$bytes = 0;
	if ( $i{dtype} && $i{dtype} !~ /ARG_NONE/  ) {
		$bytes += disasm_operand_decode( \%i, "dest", $buf, $bytes );
	} else { $i{dtype} = $i{dest} = ""; }
	if ( $i{stype} && $i{stype} !~ /ARG_NONE/ ) {
		$bytes += disasm_operand_decode( \%i, "src", $buf, $bytes );
	} else { $i{stype} = $i{src} = ""; }
	if ( $i{atype} && $i{atype} !~ /ARG_NONE/) {
		$bytes += disasm_operand_decode( \%i, "aux", $buf, $bytes );
	} else { $i{atype} = $i{aux} = ""; }
	$size += $bytes;

	$i{size} = $size;

	return \%i;
}

sub disasm_buffer {
	local($buf) = shift;
	local($offset) = shift;
	local($va) = shift;
	local($max) = shift;
	local($follow) = shift;
	local($hex_str) = "";
	local($pos, $dis_buf, $insn, $x, $n_va, $n_off);


	$dis_buf = substr $$buf, $offset;
	if ( $follow ) {
		$opt_quiet || printf "Disassembling forward from %08X\n", $va;
	}
	for ( $pos = 0; $pos < $max; $pos += $$insn{size} ) {
		# do not disassemble twice
		$insn = $$target_info{insn_idx}{$va + $pos};
		if ($$insn{size}) {
			$dis_buf = substr $dis_buf, $$insn{size};
			next;
		};
		# get insn hash from disassembler
		$insn = disasm_addr( $dis_buf, $max ); 

		if ( ! $insn ) {
			# invalid instruction -- skip a byte and cont
			$$insn{size} = 1;
			$dis_buf = substr $dis_buf, 1;
			next;
		};
			
		$$insn{va} = $va + $pos;
		$$insn{offset} = $offset + $pos;

		# store hexadecimal bytes representing insn in an array
		$hex_str = "";
		for ( $x = 0; $x < $$insn{size}; $x ++ ) {
			$hex_str .= "C";
		}
		@{$$insn{bytes}} = unpack $hex_str, $dis_buf;

		# add insn to list
		push(@{$target_info{insns}}, $insn);
		$$target_info{insn_idx}{$$insn{va}} = $insn;

		# check operands for data
		disasm_check_insn( $insn );

		# follow flow of execution
		if ( $$insn{type} =~ /(CALL)|(BRANCH)/ ) {
			$n_va = disasm_branch_target( $insn, $$insn{dest},
						$$insn{dtype} );
			if ( disasm_is_code_addr( $n_va ) ) {
				$n_off = disasm_va2off($n_va);
				if ( $$insn{type} =~ /CALL/ ) {
					# create function
					new_func( $n_va, $n_off, 0 );
				} else {
					new_name( $n_va, "loc_$va", "LABEL" );
				}
				new_xref( $$insn{va}, $n_va, "x" );
				if ( $follow ) {
					disasm_buffer( $buf, $n_off, $n_va, 
							$size, 1 );
				}
			}
		}

		if ( $follow && $$insn{type} =~/(RET)|(BRANCH\W)/ ) {
			# stop disassembly
			$pos = $max;
		}

		# advance buffer position
		$dis_buf = substr $dis_buf, $$insn{size};
	}
}

sub disasm_section {
	local($sec) = shift;
	local($buf) = shift;

	$opt_quiet || print "Disassembling Section $$sec{name}\n";
	disasm_buffer($buf, $$sec{offset}, $$sec{va}, $$sec{size}, 0);
	return(1);
}

#===============================================================================
# Output routines

sub print_usage {
	print "x86 Disassembler: IA32 disassembler based on libdisasm.so\n";
	print "                  (c) 2002 the bastard disassembler project\n";
	print "                  http://bastard.sourceforge.net\n";
	print "Usage:\tx86disam.pl [options...] file\n";
	print "\t\t-c Output intermediate code\n";
	print "\t\t-i Output Intel syntax\n";
	print "\t\t-a Output AT\&T syntax [default]\n";
	print "\t\t-f Disassemble forward from entry point\n";
	print "\t\t-t Disassemble executable code sections [default]\n";
	print "\t\t-p Use program headers for ELF info\n";
	print "\t\t-s Use section headers for ELF info [default]\n";
	print "\t\t-x Display cross-references\n";
	print "\t\t-q Suppress visual feedback\n";
	print "\tAdvanced options:\n";
	print "\t\t-e entry\tDisassemble from address 'entry'\n";
	print "\t\t-S section\tDisassemble section named 'section'\n";
	print "\t\t-H number\tNumber of hexadecimal bytes to print\n";
}

sub addr_format {
	local($addr) = shift;
	local($type) = shift;
	local($insn, $sym);
	
	#if va->insn_idx->name
	$insn =  $$target_info{insn_idx}{$addr};
	if ( $$insn{name} ) {
		return $$insn{name};
	}

	#if symbol [e.g. import] return symbol name
	$sym =  $$target_info{sym_idx}{$addr};
	if ( $$sym{name} ) {
		return $$sym{name};
	}

	# else, just print address
	if ( $opt_intel ) {
		return sprintf "0x%08X", $addr;
	} else {
		if ( $type =~ /OP_ADDR/ ) {
			return sprintf "*0x%08X", $addr;
		}
		if ( $type =~ /DISP32/ )  {
			return sprintf "0x%08X", $addr;
		}
		return sprintf "\$0x%X", $addr; 
	}
}

sub op_format {
	local($next_va) = shift;
	local($op) = shift;
	local($optype) = shift;
	local($disp,$scale,$index,$base,$flags,$buf);

	if ( $optype =~ /OP_REL/ ) {
		$op += $next_va;
		$optype =~ s/OP_REL/OP_ADDR/;
	}

	if ( $optype =~ /OP_OFF/ ) {
		# these are either offets to eip or va's
		if ( $optype =~ /OP_BYTE/ ) {
			$op += $next_va;
		}
		$optype =~ s/OP_OFF/OP_ADDR/;
	}

	if ( $opt_intel ) {
		if ( $optype =~ /OP_EADDR/ ) {
			($disp,$scale,$index,$base,$flags) = split /:/, $op;
			$buf = "[";
			if ( $base ) { $buf .= $base; }
			if ( $index ) {
				if ( $base ) { $buf .= "+"; }
				if ( $scale ) {
					$buf .= "($index*$scale)";
				} else {
					$buf .= $index;
				}
			}
			if ( $disp ) {
				if ( $base || $index ) { $buf .= "+"; }
				if ( $flags =~ /DISP32/ ) {
					$buf .= addr_format($disp, "DISP32");
				} else {
					$buf .= $disp;
				}
			}
			$buf .="]";
			return $buf;
		}
		if ( $optype =~ /OP_IMM/ ) { 
			if ( $optype =~ /OP_SIGNED/ || $optype =~ /OP_BYTE/ ) {
				return sprintf "%d", $op; 
			} else {
				return addr_format($op, "OP_IMM");
			}
		}
		if ( $optype =~ /OP_ADDR/ ) {
			return addr_format($op, "OP_ADDR");
		}
		return $op;
	} else {
		if ( $optype =~ /OP_EADDR/ ) {
			($disp,$scale,$index,$base,$flags) = split /:/, $op;
			if ( $flags =~ /DISP32/ ) {
				$buf =  addr_format($disp, "DISP32");
			} else {
				$buf = $disp;
			}
			$buf .= "($base";
			if ( $index ) {
				$buf .= ",$index";
			}
			if ($scale) {
				$buf .= ",$scale";
			} elsif ( $disp && ! $base && ! $index ) {
				# AT&T/GNU as 'syntax exception
				$buf .= ",1";
			}
			$buf .= ")";
			return $buf;
		}
		if ( $optype =~ /OP_REG/ ) { return "%$op"; }
		if ( $optype =~ /OP_IMM/ ) { 
			if ( $optype =~ /OP_SIGNED/ ||
			     $optype =~ /OP_BYTE/ ) {
				return sprintf "\$%d", $op; 
			} else {
				return addr_format($op, "OP_IMM");
			}
		}
		if ( $optype =~ /OP_ADDR/ ) {
			return addr_format($op, "OP_ADDR");
		}
	}
	return $op;
}

sub insn_format {
	local($insn) = shift;
	local($optype) = shift;

	if ( $opt_intel ) {
		return $insn;
	}
	if ($optype =~ /OPSIZE_([A-Z0-9]+)/) {
		if ( $1 =~ /BYTE/ && $insn !~ /^j/ ) {
			return $insn . "b";
		} elsif ($1 =~ /HWORD/ && $insn !~ /^j/ ) {
			return $insn . "w";
		} elsif ($1 =~ /DWORD/ && $insn !~ /^j/ ) {
			return $insn . "q";
		} elsif ($1 =~ /^WORD/ ) {
			if ( $insn eq "jmp" ) { return "ljmp"; }
			if ( $insn eq "call" ) { return "lcall"; }
			if ( $insn =~ /^j/ ) {return $insn; }
			return $insn ."l";
		}
	}
	return $insn;
}

sub insn_prefix {
	local($type) = shift;
	local($prefix);

	if ($type =~ /INS_LOCK/ ) {
		$prefix .= "lock ";
	} elsif ( $type =~ /INS_REPNZ/ ) {
		$prefix .= "repnz ";
	} elsif ( $type =~ /INS_REPZ/ ) {
		$prefix .= "repz ";
	} elsif ( $type =~ /SEG_([A-Z]+)/ ) {
		$prefix .= lc($1) . ": ";
	}
	return $prefix;
}

sub addr_output {
	local($va) = shift;
	local($size) = shift;
	local($bytes) = shift;
	local($name) = shift;
	local($x);

	if ( $name ) { print "$name:\n"; }
	printf "%08X: ", $va;
	for ($x = 0; $x < $opt_hexbytes; $x++ ) {
		if ( $x < $size ) { printf "%02X ",$$bytes[$x]; }
		else { print "   "; }
	}
	return;
}

sub insn_output {
	local($insn) = shift;
	local($from, $func);

	$func = $$target_info{func_idx}{$$insn{va}};
	if ( $$func{va} ) {
		print "\n";
		print ";-------------------------------------------\n";
		print "; Subroutine $$func{name}\n\n";
	}

	addr_output( $$insn{va}, $$insn{size}, $$insn{bytes}, $$insn{name} );

	if ( $$insn{mnemonic} ) {
		printf "\t%s", insn_prefix($${type});
		printf "%s\t", insn_format($$insn{mnemonic}, $$insn{dtype});
		if ( $opt_intel ) {
			if ( $$insn{dtype} ) {
				printf "%s", op_format($$insn{va}+$$insn{size},
						$$insn{dest}, $$insn{dtype});
				if ( $$insn{stype} ) {
					printf ", %s", op_format($$insn{va} + 
						$$insn{size}, $$insn{src}, 
						$$insn{stype} );
				}
				if ( $$insn{atype} ) {
					printf ", %s", op_format($$insn{va} +
						$$insn{size}, $$insn{aux}, 
						$$insn{atype} );
				}
			}
		} else {
			if ( $$insn{stype} ) {
				printf "%s", op_format($$insn{va}+$$insn{size}, 
						$$insn{src}, $$insn{stype} );
			}
			if ( $$insn{dtype} ) {
				if ( $$insn{stype} ) { print ", "; }
				printf "%s", op_format($$insn{va}+$$insn{size}, 
						$$insn{dest}, $$insn{dtype});
			}
			if ( $$insn{atype} ) {
					printf ", %s", op_format($$insn{va} + 
						$$insn{size}, $$insn{aux}, 
						$$insn{atype} );
			}
		}
		print "\n";

		# print xrefs
		if ( $opt_xref ) {
			foreach ( xrefs_to($$insn{va}) ) {
				$from = addr_format($$_{from}, "OP_ADDR");
				$from =~ s/^\*//g;
				print "\t\t\t\t\t; ";
				print "XREF ($$_{type}) from $from\n";
			}
		}

		# print an extra line if this was a ret
		print "\n\n" if ( $$insn{type} =~ /INS_RET/ );
		print "\n" if ( $$insn{type} =~ /(INS_BRANCH)|(INS_CALL)/ );
		
	} else { 
		print "\t<invalid instruction>\n";
	}
	return;
}

sub data_output {
	local($data) = shift;
	local($string, $from);

	addr_output( $$data{va}, $$data{size}, $$data{bytes}, $$data{name} );

	if ( $$data{type} =~ "STRING" ) {
		$string = $$target_info{string_idx}{$$data{va}};
		print "\t ; String '$$string{string}'";
	}
	print "\n";
	# print xrefs
	if ( $opt_xref ) {
		foreach ( xrefs_to($$data{va}) ) {
			$from = addr_format($$_{from}, "OP_ADDR");
			$from =~ s/^\*//g;
			print "\t\t\t\t\t; XREF ($$_{type}) from $from\n";
		}
	}
	return;
}

sub asm_output {
	foreach ( keys( %{$$target_info{sections}} ) ) {
		$sec = $$target_info{sections}{$_};
		print ";--------------------------------------------------\n";
		printf "; SECTION %s va %08X size 0x%X\n\n", $$sec{name}, 
					$$sec{va}, $$sec{size};
		if ( $$sec{type} =~ /CODE/ ) {		# print code
			foreach( sort keys( %{$$target_info{insn_idx}} ) ) {
				if ( $_ >= $$sec{va} && 
				     $_ < $$sec{va} + $$sec{size} ) {
					insn_output( 
						$$target_info{insn_idx}{$_}  );
				}
			}
		} else {				# print data
			foreach( sort keys( %{$$target_info{data_idx}} ) ) {
				if ( $_ >= $$sec{va} && 
				     $_ < $$sec{va} + $$sec{size} ) {
					data_output( 
						$$target_info{data_idx}{$_}  );
				}
			}
		}
		print "\n\n\n";		# mark end of section
	}
	return;
}

# Output intermediate code: Just output every field of all hashes
sub int_output {
	local($ptr);
	local($buf);

	# print target info
	print "#TARGET|name|entry_va|entry_offset|size|endian|bits\n";
	printf "TARGET|%s|0x%08X|0x%X|$d|%d|%s\n", $target_info{name},
		$target_info{entry}, $target_info{entry_offset}, 
		$target_info{size}, $target_info{endian}, $target_info{bits};

	# print sections
	print "#SEC|va|offset|size|type|perm|name\n";
	foreach ( keys( %{$$target_info{sections}} ) ) {
		$ptr = $$target_info{sections}{$_};
		printf "SEC|0x%08X|0x%X|%d|", $$ptr{va}, $$ptr{offset}, 
			$$ptr{size};
		print "$$ptr{type}|$$ptr{perm}|$$ptr{name}\n";
	}

	# print symbols
	print "#SYM|va|offset|type|name\n";
	foreach $ptr ( @{$target_info{symbols}} ) {
		printf "SYM|0x%08X|0x%X|", $$ptr{va}, $$ptr{offset};
		print "$$ptr{type}|$$ptr{name}\n";
	}

	#print names
	print "#NAME|va|type|name\n";
	foreach $ptr ( @{$target_info{names}} ) {
		printf "NAME|0x%08X|%s|%s\n", $$ptr{va}, $$ptr{type}, 
			$$ptr{name};
	}

	# print instructions
	print "#INSN|va|offset|size|hex|mnemonic|type|";
	print "src|type|perm|dest|type|perm|aux|type|perm|flags|name\n";
	foreach $ptr ( @{$target_info{insns}} ) {
		$buf = "";
		printf "INSN|0x%08X|0x%X|%d|",$$ptr{va},$$ptr{offset},
			$$ptr{size};
		foreach ( @{$$ptr{bytes}} ) { $buf .= sprintf "%02X ", $_; }
		$buf =~ s/\s*$//g;
		print "$buf|$$ptr{mnemonic}|$$ptr{type}|";
		print "$$ptr{src}|$$ptr{stype}|$$ptr{sprm}|";
		print "$$ptr{dest}|$$ptr{dtype}|$$ptr{dprm}|";
		print "$$ptr{aux}|$$ptr{atype}|$$ptr{aprm}|";
		$buf = $$ptr{flags};
		if ( $buf eq "0" ) {
			$buf = "";
		} else {
			$buf =~ s/\|/,/g;
		}
		print "$buf|$$ptr{name}";
		print "\n";
	}

	#print functions
	print "#FUNC|va|offset|name\n";
	foreach $ptr ( @{$target_info{functions}} ) {
		printf "FUNC|0x%08X|0x%X|%s\n", $$ptr{va}, $$ptr{offset}, 
			$$ptr{name};
	}

	#print data
	print "#DATA|va|offset|size|bytes|type|name\n";
	foreach $ptr ( @{$target_info{data}} ) {
		printf "DATA|0x%08X|0x%X|%d|", $$ptr{va}, $$ptr{offset}, 
			$$ptr{size};
		$buf="";
		foreach ( @{$$ptr{bytes}} ) { $buf .= sprintf "%02X ", $_; }
		$buf =~ s/\s*$//g;
		print "$buf|$$ptr{type}|$$ptr{name}\n";
	}

	#print strings
	print "#STRING|va|offset|string\n";
	foreach $ptr ( @{$target_info{strings}} ) {
		$$ptr{string} =~ s/\n/\\n/;	# escape newlines :)
		printf "STRING|0x%08X|0x%X|%s\n", $$ptr{va}, $$ptr{offset}, 
				$$ptr{string};
	}

	#print xrefs
	print "#XREF|from|to|type|name\n";
	foreach $ptr ( @{$target_info{xrefs}} ) {
		printf "XREF|0x%08X|0x%08X|%s|%s\n", $$ptr{from}, $$ptr{to}, 
				$$ptr{type}, $$ptr{name};
	}

	return;
}
