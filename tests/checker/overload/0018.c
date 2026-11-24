#define O __attribute__((overloadable))
void g(void){{int f(int) O __attribute__((ms_abi));} {int f(int) O __attribute__((sysv_abi));}}
