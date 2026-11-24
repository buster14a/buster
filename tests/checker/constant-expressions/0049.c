struct S { int x; }; enum { N=sizeof(struct __attribute__((aligned(3))) S) };
