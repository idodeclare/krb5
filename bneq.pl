#!/usr/bin/env perl

use strict;
use warnings;
use FileHandle;
use File::Basename;
our (%t);

die "Usage: $0 <targets.txt> [ _filename_ ... ]\n" if @ARGV < 1;

my $tf = $ARGV[0];
read_targets($tf) or die "ERROR reading $tf\n";

for (my $i = 1; $i < @ARGV; ++$i) {
	my $f = $ARGV[$i];
	my ($n, $p) = fileparse($f);
	my $ms = exists $t{$n} ? $t{$n} : ["NOTFOUND"];
	$ms = sort_matches($ms, $f);
	my $pf = $f;
	foreach my $m (@$ms) {
		printf("%s\t%s\n", $pf, $m);
		$pf = "";
	}
}

#-----------------------------------------------------------------------------

sub sort_matches {
	my ($ms, $f) = @_;
	return $ms if @$ms < 2;
	my %mapparts = map { ($_, 1) } grep { $_ ne "" } split m`/`, $f;
	my @newms = map { [$_, 10 * count_alikes($_, \%mapparts) +
	    ($_ =~ /krb/ ? 1 : 0) + ($_ =~ /gss/ ? .1 : 0) ] } @$ms;
	@newms = map { $_->[0] }
	    sort {
		my $cmp = $b->[1] <=> $a->[1];
		if ($cmp == 0) {
			$cmp = $a->[0] cmp $b->[0];
		}
		$cmp;
	    } @newms;
	return \@newms;
}

sub count_alikes {
	my ($f, $mapparts) = @_;
	my @n = grep { $_ ne "" && exists $mapparts->{$_} } split m`/`, $f;
	return scalar(@n);
}

sub read_targets {
	my ($f) = @_;

	open(my $fh, "<", $f) or die "Can't open < $f: $!";
	my $line;
	while (defined($line = <$fh>)) {
		chomp $line;
		my ($n, $p) = fileparse($line);
		my $ms;
		if (!exists $t{$n}) {
			$t{$n} = $ms = [];
		} else {
			$ms = $t{$n};
		}
		push @$ms, $line;
	}
	return 1;
}
