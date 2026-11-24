#define f(x) x
#define g() 1
#define h(y) y
f(a
#if g()
#endif
h(
#pragma foo
)
#pragma bar
b)
