#define O __attribute__((overloadable))
int f(int) O;
int f(int);
