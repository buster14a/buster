#define O __attribute__((overloadable))
int f(int) O; double f(double) O;
__typeof__(f) p;
