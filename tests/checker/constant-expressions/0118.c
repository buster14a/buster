struct I { char c; int x; }; struct S { int n; struct I a[2]; }; enum { A=__builtin_offsetof(struct S,a[1].x) }; _Static_assert(A==16, "offset");
