#define O __attribute__((overloadable))
int f(int) O; double f(double) O; _Static_assert(_Generic(f(f(1.0)),double:1,default:0),"nested");
