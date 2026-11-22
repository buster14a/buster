// #2219: failed static assertions print an imaginary constant as clang's StmtPrinter does: its imaginary part's literal,
// its value in decimal with its type's suffix, then i, whichever order its suffixes were written in.
_Static_assert(5iwb == 4, "");
_Static_assert(5uwbi == 4, "");
_Static_assert(0x1ffffffffffffffffffwbi == 4, "");
_Static_assert(0x10i == 4, "");
_Static_assert(07lli == 4, "");
