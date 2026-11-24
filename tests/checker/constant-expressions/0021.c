#define O __attribute__((overloadable))
int f(int) O __attribute__((ms_abi)); double f(double) O; typedef int T [[gnu::aligned(sizeof((int (__attribute__((ms_abi)) *)(int))f))]]; _Static_assert(_Alignof(T)==8,"raw ABI target");
