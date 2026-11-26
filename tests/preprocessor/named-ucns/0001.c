int \N{LATIN SMALL LETTER E WITH ACUTE} = 1;
int f(void) { return é; }
int g(void) { return \u00e9 + \U000000E9 + \u{e9}; }
