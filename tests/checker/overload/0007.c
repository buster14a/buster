#define O __attribute__((overloadable))
int f(int) O; double f(double) O; int f(int) O; _Static_assert(_Generic(f(1),int:1,default:0),"redecl");
