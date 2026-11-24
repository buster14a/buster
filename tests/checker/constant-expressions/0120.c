struct S { int x; }; enum { A=__builtin_offsetof(struct S,missing) };
