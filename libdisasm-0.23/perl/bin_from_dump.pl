#!/usr/bin/perl

my $filename = shift;

open(INFILE, $filename);
open(OUTFILE, ">>bin.out");

foreach (<INFILE>) {
   if (/ [0-9a-fA-F]{7}:\s+(([0-9a-fA-F]{2}\s)+)\s+[A-z]+.*$/){
      $bytes = $1;
      $bytes =~ s/\s+$//g;
      foreach ( split(/ /, $bytes) ){
         printf OUTFILE "%c", hex($_);
      }
   }
}
close(INFILE);
close(OUTFILE);
