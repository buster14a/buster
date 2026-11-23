#define O __attribute__((overloadable))
int (*f(int))(int) O; int (*f(int))() O; int (*f(int))(double) O;
