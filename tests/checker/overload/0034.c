#define O __attribute__((overloadable))
int f(const int*const*) O; double f(void*) O; _Static_assert(_Generic(f((int**)1),int:1,default:0),"qualified");
