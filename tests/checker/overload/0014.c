#define O __attribute__((overloadable))
int f(int) __attribute__((unavailable)); int f(int); double f(double) O; int f(int); void g(void){f(1);}
