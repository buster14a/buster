#define O __attribute__((overloadable))
int f(int) O; double f(double) O; __typeof__((int(*)(int))f) p; _Static_assert(__builtin_types_compatible_p(__typeof__(p),int(*)(int)),"typed address");
