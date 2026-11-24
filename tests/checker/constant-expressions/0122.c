struct S { char c; int a[3]; }; _Static_assert(__builtin_offsetof(struct S,a[2])==12, "offset");
