struct S { int x; }; enum { N=sizeof(struct __attribute__((aligned(1/0))) S) };
