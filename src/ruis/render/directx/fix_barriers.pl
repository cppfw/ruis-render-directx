#!/usr/bin/perl
local $/;
my $content = <STDIN>;
$content =~ s/^\t+ \. "&transition_barrier\("\r?\n/\t\t\t\t&transition_barrier(\n/gm;
print $content;
