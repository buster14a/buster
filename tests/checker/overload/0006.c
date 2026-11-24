#define O __attribute__((overloadable))
int f(int) O; double f(double) O; __typeof__(f(1.0)) value; _Static_assert(__builtin_types_compatible_p(__typeof__(value),double),"typeof");
