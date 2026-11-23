#define O __attribute__((overloadable))
int f(int x) O{return x;} double f(double) O; int f(int) O; int f(int x) O{return x;}
