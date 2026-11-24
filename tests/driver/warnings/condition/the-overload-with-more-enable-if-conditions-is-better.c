int m(int a, long x) __attribute__((overloadable, enable_if(a > 0, "a")));
int m(int a, long x) __attribute__((overloadable, enable_if(a > 0, "a")));
char m(int b, long long y) __attribute__((overloadable, enable_if(b > 0x0, "b"), enable_if(b < 9, "c")));
_Static_assert(sizeof(m(1, 0)) == 1, "more");
_Static_assert(sizeof(m(10, 0)) == 4, "fewer");
