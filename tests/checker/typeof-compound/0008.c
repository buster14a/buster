struct S { long x; }; int f(void) { __typeof__(((struct S){0}).x) x = 0; _Static_assert(sizeof x == sizeof(long), "member"); return x; }
