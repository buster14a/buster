#define O __attribute__((overloadable))
int f(int) O; double f(double) O; typedef int T [[gnu::aligned(sizeof((int(*)(int))f))]]; _Static_assert(_Alignof(T)==8,"raw cast");
