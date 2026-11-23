#define O __attribute__((overloadable))
int f(int); double f(double) O; int f(char);
