#define O __attribute__((overloadable))
int f(int) O; static int f(int) O;
