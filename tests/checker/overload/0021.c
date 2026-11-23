#define O __attribute__((overloadable))
int f(int); double f(double) O; _Static_assert(_Generic(f(1),int:1,default:0),"unmarked"); _Static_assert(_Generic(f(1.0),double:1,default:0),"marked");
