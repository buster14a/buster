#define O __attribute__((overloadable))
typedef int A __attribute__((aligned(16))); int f(A**) O; int f(int**) O; _Static_assert(_Generic(f((int**)0),int:1,default:0),"aligned");
