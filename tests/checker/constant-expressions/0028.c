#define O __attribute__((overloadable))
int f(int) O; double f(double) O;
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_constant_p(f)),int),"ambiguous");
