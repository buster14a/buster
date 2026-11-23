#define O __attribute__((overloadable))
long f(void) O; int f(int) O; typedef int T [[gnu::aligned(sizeof(f()))]]; _Static_assert(_Alignof(T)==8,"raw zero call");
