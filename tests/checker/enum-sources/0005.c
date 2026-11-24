enum { A=8, N=A+sizeof(struct __attribute__((aligned(A))) S { int x; }) }; _Static_assert(N==16 && _Alignof(struct S)==8,"header attributes");
