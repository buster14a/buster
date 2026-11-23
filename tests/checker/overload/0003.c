#define O __attribute__((overloadable))
int f(int) O; _Static_assert(_Generic(f(1.0),int:1,default:0),"before"); double f(double) O; _Static_assert(_Generic(f(1.0),double:1,default:0),"after");
