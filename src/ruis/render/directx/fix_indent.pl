my @lines = <>;
for my $i (0..$#lines) {
    if ($lines[$i] =~ s/^(-?&|&)(transition_barrier\().*$/"\t" x ($1_tabs+4) . "&$2" . "PLACEHOLDER" . "\n"/e) {
        # find tabs from previous line
    }
}
print @lines;
