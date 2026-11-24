union U { int i; unsigned u; }; static const union U v={.i=7}; enum { E=__builtin_popcount(v.u) };
