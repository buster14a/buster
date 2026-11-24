#define O __attribute__((overloadable))
void g(void){{int f(int) O __attribute__((ms_abi));} {int f(int) O; int (__attribute__((ms_abi))*p)(int)=f;}}
