#define O __attribute__((overloadable))
int f(int) O; double f(double) O; int take(int(*)(int)) O; double take(char*) O; typedef int T [[gnu::aligned(sizeof(take(f)))]]; _Static_assert(_Alignof(T)==4,"raw context");
