struct S { char c; int a[3]; }; enum { A=__builtin_offsetof(struct S,a[2]) }; _Static_assert(A==12, "offset");
