struct S { unsigned x:3; }; enum { A=__builtin_offsetof(struct S,x) };
