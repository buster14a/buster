#define O __attribute__((overloadable))
int f(int x){return x;} int f(int); double f(double) O; int f(int x){return x;}
