#!/usr/bin/perl

my %tables;
my @table_table;
my ($i, $name, $current_table, $entries);
my $last_line = "";
my $pfx="ia32";

print "#include \"$pfx" . "_insn.h\"\n\n";
print "#include \"$pfx" . "_reg.h\"\n\n";
print "#include \"$pfx" . "_opcode_tables.h\"\n\n";

$i = 0;
open(FILE, shift());
foreach (<FILE>){
	chomp;
	if ( /^TABLE ([A-Za-z0-9_]+)\t(tbl_[a-z_]+)\tshift ([0-9]+)\tmask ([0-9]+)\tminlim ([0-9]+)\tmaxlim ([0-9]+)\t"([^"]*)"$/ ) {
		print "static $pfx" . "_insn_t $1";
		print "[] = {\t/* $7 */\n";
		$table_table[$i] = {};
		$current_table = $table_table[$i];
		$$current_table{'name'} = $1;
		$$current_table{'type'} = $2;
		$$current_table{'index'} = $i; #= $3;
		$$current_table{'shift'} = $3;
		$$current_table{'mask'} = $4;
		$$current_table{'min'} = $5;
		$$current_table{'max'} = $6;
		$$current_table{'descr'} = $7;
		$$current_table{'prefix'} = "pfx_" . $1;
		$$current_table{'prefix_entries'} = [];
		$i = $i + 1;
		
	} elsif ( /^INSN (.+)$/ ) {
		if ( $last_line ne "" ) {
			print "$last_line,\n";
		}
		
		#Table|MnemFlag|DestFlag|SrcFlag|AuxFlag|CPU|mnem|dest|src|aux|flags_effected|ipmlicit|cmt
		($t,$mf,$df,$sf,$af,$cpu,$m,$d,$s,$a,$flg,$imp,$cmt) = 
			split '\t', $1;

		# -------------------------------------------------
		# OK, add some custom code here to modify the insn
		# -------------------------------------------------

		$last_line = "\t$cmt { $t, $mf, $df, $sf, $af, $cpu, ";
		$last_line = $last_line . $m . ", $d, $s, $a, $flg, $imp }";

	} elsif ( /^END TABLE/ ) {
		if ( $last_line ne "" ) {
			print "$last_line\n";
		}
		$last_line = "";
		print "};\n\n\n";
	} elsif ( /^#/ or /^\s*$/ ) {
		next;
	}
}
close(FILE);

print "\n/* ================== Table of Opcode Tables ================== */\n";

print $pfx . "_table_desc_t $pfx" . "_tables[] = {\n";
print "\t/* table, prefix table, type, shift, mask, min, max */\n";
foreach ( @table_table ) {
	
	if ( $$_{'index'} and !($$_{'index'} % 5) ) {
		print "\t/* $$_{index} */\n";
	}

	printf "\t{ %s, %s, 0x%02X, 0x%02X, 0x%02X, 0x%02X }",
		$$_{'name'} , $$_{'type'} , 
		$$_{'shift'} , $$_{'mask'} , $$_{'min'} , $$_{'max'} ;
	if ( $_ ne $table_table[ $#table_table ] ) {
		print ",";
	}

	print "\n";
}
print "};\n";

# print table indices in a comment */
print "/* ia32_opcode_tables.h */\n";
print "/* Table index constants: \n";
foreach ( @table_table ) {
	$name = $$_{'name'};
	$name =~ s/tbl_/idx_/;
	print "#define $name " . $$_{'index'} . "\n";
}

print "*/ \n";
