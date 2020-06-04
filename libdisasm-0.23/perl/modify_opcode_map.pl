#!/usr/bin/perl
# Quickhack script for making modifications to the opcode table

my %tables;
my @table_table;
my $line = "";
my $pfx="ia32";

open(FILE, shift());
foreach (<FILE>){
	chomp;
	if ( /^TABLE ([A-Za-z0-9_]+)\tindex ([0-9]+)\tshift ([0-9]+)\tmask ([0-9]+)\tminlim ([0-9]+)\tmaxlim ([0-9]+)/ ) {
		# code to handle table start
		$table_table[$2] = sprintf 
			"{ $1, 0x%02X, 0x%02X, 0x%02X, 0x%02X } /* $2 */",
				$3, $4, $5, $6;
		print;
	} elsif ( /^INSN (.+)$/ ) {
		# code to handle insn
		#Table|MnemFlag|DestFlag|SrcFlag|AuxFlag|CPU|mnem|dest|src|aux|flags_effected|cmt
		($t,$mf,$df,$sf,$af,$cpu,$m,$d,$s,$a,$flg,$cmt) = 
			split '\t', $1;

		# -------------------------------------------------
		# OK, add some custom code here to modify the insn
		# -------------------------------------------------


		$line = "$t\t$mf\t$df\t$sf\t$af\t$cpu\t";
		$line = $line . "$m\t$d\t$s\t$a\t$flg\t$cmt";
		print "INSN $line\n";

	} elsif ( /^END TABLE/ ) {
		# code to handle table end
		print;
	} elsif ( /^#/ or /^\s*$/ ) {
		# code to handle comments and blank lines
		print;
	} else {
		# default: print line
		print;
	}
}
close(FILE);

