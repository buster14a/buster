#define O __attribute__((overloadable))
void g(void){{int f(int) O;} {double f(int) O;}}
