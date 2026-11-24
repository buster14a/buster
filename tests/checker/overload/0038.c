#define O __attribute__((overloadable))
int f(int) O; double f(double) O; int(*take)(int(*)(int)); _Static_assert(_Generic(take(&f),int:1,default:0),"pointer context");
